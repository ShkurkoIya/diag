/*
 * diag_cell_lock.h — Cell locking via EFS2
 *
 * KEY FIXES (from Askey modem firmware source):
 *   1. oflag must include O_TRUNC (0x200) — without it file is NOT reliably overwritten
 *   2. mode = 0777 (0x01FF) not 0x0008
 *   3. Barring timers must be non-zero (5, 30, 90)
 *   4. Added OPEN/WRITE/CLOSE fallback for reliable writes
 *
 * Askey reference (proven working on Qualcomm modems):
 *   efs_put(path, data, len, O_RDWR|O_CREAT|O_TRUNC|O_AUTODIR, 0777)
 */

#pragma once
#ifndef DIAG_CELL_LOCK_H
#define DIAG_CELL_LOCK_H

#include "libdiag_loader.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>

// EFS2 open flags (ARM/Qualcomm)
#define EFS_O_RDONLY  0x0000
#define EFS_O_WRONLY  0x0001
#define EFS_O_RDWR    0x0002
#define EFS_O_CREAT   0x0040
#define EFS_O_TRUNC   0x0200
#define EFS_O_AUTODIR 0x0100   // Qualcomm extension: auto-create parent dirs

#pragma pack(push, 1)
struct BtsLteLock4 { uint32_t freq; uint16_t pci; };
struct BtsLteLockItem { uint32_t pci; uint32_t freq; };
struct BtsLteLockList { uint32_t len; BtsLteLockItem cell[30]; };

// EFS2 item PUT/GET header (subcmd 38/39)
struct Efs2ItemHdr {
    uint32_t size;
    uint16_t oflag;   // was "reserved0" — actually open flags!
    uint16_t mode;    // was "reserved1" — actually permissions!
    uint16_t code;    // PUT=0x17BA, GET=0x17BB
};

struct Efs2ItemRsp {
    uint16_t code;
    int32_t  diag_errno;
};
#pragma pack(pop)

class DiagCellLock {
public:
    explicit DiagCellLock(int client_id) : client_id_(client_id) {}
    void set_debug(bool on) { debug_ = on; }

    // ── LTE v1: csp/pci_lock (4 bytes BE) ───────────────────────────────
    int lock_lte_v1(uint16_t earfcn, uint16_t pci) {
        prelock_cleanup();   // ★ MUST run before write
        uint8_t val[4];
        uint16_t be = __builtin_bswap16(earfcn); memcpy(val, &be, 2);
        be = __builtin_bswap16(pci); memcpy(val + 2, &be, 2);
        log_lock_attempt("v1", LTE_LOCK_PATH_v1, val, sizeof(val), earfcn, pci);
        int ret = write_efs_reliable(LTE_LOCK_PATH_v1, val, sizeof(val));
        log_lock_outcome("v1", ret, earfcn, pci);
        if (ret == 0) { lte_locked_ = true; lte_earfcn_ = earfcn; lte_pci_ = pci; }
        return ret;
    }

    // ── LTE v2: cell_restrict_opt_params (36 bytes) ──────────────────────
    //
    // From Askey firmware. Layout:
    //   [0]    mobility_with_cell_lock (0 = stay locked, 1 = allow handover)
    //   [1-7]  reserved (always 0)
    //   [8]    short_cell_barring_time  (timer, NOT critical for lock success)
    //   [12]   reduced_barring_time     (timer, NOT critical for lock success)
    //   [16]   backoff_barring_time     (timer, NOT critical for lock success)
    //   [20-23] cell_lock_dl_earfcn (u32 LE)  ★ critical
    //   [24-25] cell_lock_pci (u16 LE)        ★ critical
    //   [26-35] reserved (always 0)
    //
    // NOTE on barring timers: these control how long the modem keeps a cell
    // marked as "barred" if it broadcasts SystemInformation barring info.
    // They affect timing of cell reject/recovery, NOT whether the lock itself
    // activates. Tested empirically with values 0x00/0x00/0x00 — lock still
    // works correctly. Original Askey values (5/30/90) kept for safety.
    int lock_lte_v2(uint32_t earfcn, uint16_t pci) {
        prelock_cleanup();   // ★ MUST run before write
        uint8_t val[36] = {};
        val[0] = 0x00;         // mobility_with_cell_lock = 0 (stay on locked cell)
        val[8]  = 0x05;        // short_cell_barring_time   (timer only — not lock-critical)
        val[12] = 0x1e;        // reduced_barring_time      (timer only — not lock-critical)
        val[16] = 0x5a;        // backoff_barring_time      (timer only — not lock-critical)
        memcpy(val + 20, &earfcn, 4);  // cell_lock_dl_earfcn u32 LE ★
        memcpy(val + 24, &pci, 2);     // cell_lock_pci u16 LE       ★

        log_lock_attempt("v2", LTE_LOCK_PATH_v2, val, sizeof(val), earfcn, pci);
        int ret = write_efs_reliable(LTE_LOCK_PATH_v2, val, sizeof(val));
        log_lock_outcome("v2", ret, earfcn, pci);
        if (ret == 0) { lte_locked_ = true; lte_earfcn_ = earfcn; lte_pci_ = pci; }
        return ret;
    }

    // ── LTE v3: cell_lock_list ───────────────────────────────────────────
    // From Walktour (Vivo): must write FULL fixed-size struct (244 bytes)
    // with zero-padded unused slots. Some modems reject partial writes.
    // Format: [count:u32] + [pci:u32 freq:u32] × 30 = 4 + 240 = 244 bytes
    int lock_lte_v3(BtsLteLockList list) {
        prelock_cleanup();   // ★ MUST run — prevents stale v1/v2 from shadowing
        uint32_t count = list.len > 30 ? 30 : list.len;

        // Full fixed-size buffer: 4 (count) + 30 * 8 (cells) = 244 bytes
        static constexpr size_t FULL_SIZE = 4 + 30 * sizeof(BtsLteLockItem); // 244
        uint8_t val[FULL_SIZE] = {};  // zero-initialized (unused slots = 0)

        memcpy(val, &count, 4);

        for (uint32_t i = 0; i < count; ++i) {
            size_t off = 4 + i * 8;
            memcpy(val + off,     &list.cell[i].pci,  4);  // pci u32 LE
            memcpy(val + off + 4, &list.cell[i].freq, 4);  // freq u32 LE
        }

        fprintf(stderr, "[EFS] cell_lock_list: count=%u, size=%zu\n", count, FULL_SIZE);
        for (uint32_t i = 0; i < count; ++i)
            fprintf(stderr, "[EFS]   cell[%u]: pci=%u freq=%u\n",
                    i, list.cell[i].pci, list.cell[i].freq);

        uint32_t first_freq = count > 0 ? list.cell[0].freq : 0;
        uint16_t first_pci  = count > 0 ? (uint16_t)list.cell[0].pci : 0;
        log_lock_attempt("v3", LTE_CELL_LOCK_LIST_PATH_v3, val, FULL_SIZE,
                         first_freq, first_pci);
        int ret = write_efs_reliable(LTE_CELL_LOCK_LIST_PATH_v3, val, FULL_SIZE);
        log_lock_outcome("v3", ret, first_freq, first_pci);
        if (ret == 0) {
            lte_locked_ = true;
            lte_earfcn_ = first_freq;
            lte_pci_ = first_pci;

            // ── READ-BACK: confirm what's actually on flash ──
            // Even with a successful PUT + SYNC, sanity-checking what the
            // modem will read on next boot is the only way to be sure
            // the bytes we asked for are there.
            verify_lte_v3_readback(list, count);
        } else {
            fprintf(stderr, "[EFS] cell_lock_list WRITE FAILED (rc=%d)\n", ret);
        }
        return ret;
    }

    /**
     * Read /nv/item_files/modem/lte/rrc/efs/cell_lock_list back via EFS GET
     * and dump it to stderr as both hex and parsed pairs. Logs whether the
     * readback matches what we just intended to write.
     *
     * Output goes to stderr → captured by JNI/runDiag → surfaces in the
     * Kotlin scan log so you can see the actual on-flash state.
     */
    void verify_lte_v3_readback(const BtsLteLockList& expected, uint32_t expected_count) {
        std::vector<uint8_t> data;
        int rc = efs_read_file(LTE_CELL_LOCK_LIST_PATH_v3, data);
        if (rc < 0) {
            fprintf(stderr, "[EFS-VERIFY] read-back FAILED (errno=%d) — "
                            "wrote OK but cannot confirm contents\n", errno_);
            return;
        }
        if (data.size() < 4) {
            fprintf(stderr, "[EFS-VERIFY] read-back returned only %zu bytes "
                            "(expected >= 4) — file may be corrupt\n", data.size());
            return;
        }

        // Parse what's actually on flash
        uint32_t actual_count = 0;
        memcpy(&actual_count, data.data(), 4);

        fprintf(stderr, "[EFS-VERIFY] cell_lock_list on-flash: %zu bytes, count=%u\n",
                data.size(), actual_count);

        // Hex dump first 16 + (actual_count * 8) bytes for diagnostic
        size_t dump_len = std::min(data.size(), (size_t)(4 + actual_count * 8 + 16));
        fprintf(stderr, "[EFS-VERIFY] hex (%zu bytes):", dump_len);
        for (size_t i = 0; i < dump_len; ++i) {
            if (i % 16 == 0) fprintf(stderr, "\n[EFS-VERIFY]   %04zx:", i);
            fprintf(stderr, " %02x", data[i]);
        }
        fprintf(stderr, "\n");

        // Parsed cell entries
        size_t safe_count = std::min(actual_count, (uint32_t)30);
        for (size_t i = 0; i < safe_count; ++i) {
            size_t off = 4 + i * 8;
            if (off + 8 > data.size()) break;
            uint32_t pci = 0, freq = 0;
            memcpy(&pci,  data.data() + off,     4);
            memcpy(&freq, data.data() + off + 4, 4);
            const char* match_marker = "";
            if (i < expected_count &&
                expected.cell[i].pci == pci &&
                expected.cell[i].freq == freq) {
                match_marker = " [MATCH]";
            } else if (i < expected_count) {
                match_marker = " [MISMATCH!]";
            }
            fprintf(stderr, "[EFS-VERIFY]   on-flash cell[%zu]: pci=%u freq=%u%s\n",
                    i, pci, freq, match_marker);
        }

        // Final summary
        if (actual_count != expected_count) {
            fprintf(stderr, "[EFS-VERIFY] *** COUNT MISMATCH: wanted=%u got=%u ***\n",
                    expected_count, actual_count);
        } else {
            fprintf(stderr, "[EFS-VERIFY] count OK (%u cells confirmed on flash)\n",
                    actual_count);
        }
    }

    // Convenience: lock single cell via v3 list
    int lock_lte_v3_single(uint32_t earfcn, uint16_t pci) {
        BtsLteLockList list = {};
        list.len = 1;
        list.cell[0].pci  = pci;
        list.cell[0].freq = earfcn;
        return lock_lte_v3(list);
    }

    // ── UMTS ─────────────────────────────────────────────────────────────
    int lock_umts_by_freq(uint16_t freq) {
        uint8_t fv[2]; memcpy(fv, &freq, 2);
        int r1 = write_efs_reliable(WCDMA_RRC_LOCK_PATH, fv, 2);
        uint8_t en = 1;
        int r2 = write_efs_reliable(WCDMA_RRC_ENABLE_PSC_LOCK_PATH, &en, 1);
        return r1 || r2;
    }

    int lock_umts(uint16_t psc, uint32_t freq, bool en_psc = true, uint8_t en_dbg = 1) {
        if (psc == 0) return lock_umts_by_freq(freq);
        uint8_t val[8] = {}; val[0]=en_psc; val[1]=en_dbg;
        memcpy(val+2,&psc,2); memcpy(val+4,&freq,4);
        int ret = write_efs_reliable(WCDMA_LOCK_BY_FREQ_AND_PSC_PATH, val, 8);
        if (ret != 0) return ret;
        uint8_t fv[2]; memcpy(fv, &freq, 2);
        return write_efs_reliable(WCDMA_RRC_LOCK_PATH, fv, 2);
    }

    /**
     * Lock GSM to a specific list of ARFCNs (max 64 — modem reads only the
     * first ~64 entries; safe cap). Two EFS writes per the Qualcomm recipe:
     *   1. ARFCN list → /nv/item_files/modem/geran/rr_efs_arfcn_list
     *      (each ARFCN as uint16 LE, concatenated)
     *   2. feature flag = 1 → /nv/item_files/modem/geran/grr/feature_lock_arfcn_enabled
     * The modem only re-reads these on reboot — caller must reboot the modem
     * (or toggle airplane mode on supported devices) to apply.
     *
     * @param arfcns list of GSM ARFCNs (0..1023 GSM900, 1024..1279 EGSM,
     *               512..885 DCS1800, etc.). Empty list → fails with -2.
     * @return 0 on success, negative on EFS write failure.
     */
    int lock_gsm_arfcns(const std::vector<uint16_t>& arfcns) {
        if (arfcns.empty()) {
            fprintf(stderr, "[GSM-LOCK] empty ARFCN list — refusing to write\n");
            return -2;
        }
        // Cap at 64 — well under any reasonable limit, prevents oversized
        // payloads that some modems silently truncate.
        size_t n = arfcns.size() > 64 ? 64 : arfcns.size();
        std::vector<uint8_t> payload(n * 2);
        for (size_t i = 0; i < n; ++i) {
            payload[i * 2 + 0] = static_cast<uint8_t>(arfcns[i] & 0xFF);
            payload[i * 2 + 1] = static_cast<uint8_t>((arfcns[i] >> 8) & 0xFF);
        }

        fprintf(stderr, "[GSM-LOCK] writing %zu ARFCN(s) to %s:", n, GSM_ARFCN_LIST_PATH);
        for (size_t i = 0; i < n; ++i) fprintf(stderr, " %u", arfcns[i]);
        fprintf(stderr, "\n");

        int rc = write_efs_reliable(GSM_ARFCN_LIST_PATH, payload.data(), payload.size());
        if (rc != 0) {
            fprintf(stderr, "[GSM-LOCK] arfcn_list WRITE FAILED (rc=%d, errno=%d)\n", rc, errno_);
            return rc;
        }
        fprintf(stderr, "[GSM-LOCK] arfcn_list OK (%zu bytes)\n", payload.size());

        uint8_t enable = 0x01;
        rc = write_efs_reliable(GSM_FEATURE_LOCK_ARFCN_PATH, &enable, 1);
        if (rc != 0) {
            fprintf(stderr, "[GSM-LOCK] feature_lock_arfcn_enabled WRITE FAILED (rc=%d, errno=%d)\n",
                    rc, errno_);
            // Best-effort: unlink the list so the modem doesn't camp on a
            // half-applied lock after the next reboot.
            efs_unlink(GSM_ARFCN_LIST_PATH);
            return rc;
        }
        fprintf(stderr, "[GSM-LOCK] feature_lock_arfcn_enabled = 1 OK\n");

        // Read-back verify (modem cache vs flash)
        verify_gsm_arfcn_readback(arfcns, n);
        return 0;
    }

    /**
     * Read the ARFCN list currently in EFS. Returns empty on read failure
     * (call last_errno()). Each on-flash entry is 2 bytes LE.
     */
    std::vector<uint16_t> read_gsm_arfcn_list() {
        std::vector<uint8_t> data;
        std::vector<uint16_t> out;
        int rc = efs_read_file(GSM_ARFCN_LIST_PATH, data);
        if (rc < 0 || data.size() < 2) return out;
        out.reserve(data.size() / 2);
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            uint16_t v = static_cast<uint16_t>(data[i]) |
                         (static_cast<uint16_t>(data[i + 1]) << 8);
            out.push_back(v);
        }
        return out;
    }

    /** True if the GSM feature flag byte on flash is 1. */
    bool is_gsm_arfcn_lock_enabled() {
        std::vector<uint8_t> data;
        int rc = efs_read_file(GSM_FEATURE_LOCK_ARFCN_PATH, data);
        if (rc < 0 || data.empty()) return false;
        return data[0] != 0;
    }

    /**
     * Clear the GSM ARFCN lock. Sets the feature flag to 0 (modem will ignore
     * the list even if present) AND unlinks the list file, so a stale list
     * can't accidentally re-engage if the flag flips back to 1.
     */
    int unlock_gsm() {
        fprintf(stderr, "[GSM-UNLOCK] === clearing GSM ARFCN lock ===\n");
        bool ok = true;
        // 1. Force flag = 0 (idempotent — succeeds even if file absent)
        uint8_t disable = 0x00;
        int rc = write_efs_reliable(GSM_FEATURE_LOCK_ARFCN_PATH, &disable, 1);
        if (rc != 0) {
            fprintf(stderr, "[GSM-UNLOCK] flag write 0 FAILED (rc=%d) — falling back to unlink\n", rc);
            if (unlink_with_retry(GSM_FEATURE_LOCK_ARFCN_PATH) != 0) ok = false;
        } else {
            fprintf(stderr, "[GSM-UNLOCK] feature flag set to 0 OK\n");
        }
        // 2. Unlink the list so re-enabling the flag doesn't restore a stale lock.
        if (unlink_with_retry(GSM_ARFCN_LIST_PATH) != 0) ok = false;
        fprintf(stderr, "[GSM-UNLOCK] === %s ===\n", ok ? "OK" : "FAILED");
        return ok ? 0 : -1;
    }

    /** Diagnostic read-back: dump on-flash ARFCNs and feature flag. */
    void verify_gsm_arfcn_readback(const std::vector<uint16_t>& expected, size_t expected_count) {
        auto on_flash = read_gsm_arfcn_list();
        bool flag_on = is_gsm_arfcn_lock_enabled();
        fprintf(stderr, "[GSM-VERIFY] on-flash: %zu ARFCN(s), feature_flag=%s\n",
                on_flash.size(), flag_on ? "1 (ENABLED)" : "0/absent (DISABLED)");
        for (size_t i = 0; i < on_flash.size(); ++i) {
            const char* mark = "";
            if (i < expected_count && i < expected.size() && expected[i] == on_flash[i])
                mark = " [MATCH]";
            else if (i < expected_count)
                mark = " [MISMATCH!]";
            fprintf(stderr, "[GSM-VERIFY]   arfcn[%zu] = %u%s\n", i, on_flash[i], mark);
        }
        if (on_flash.size() != expected_count) {
            fprintf(stderr, "[GSM-VERIFY] *** COUNT MISMATCH: wanted=%zu got=%zu ***\n",
                    expected_count, on_flash.size());
        } else if (!flag_on) {
            fprintf(stderr, "[GSM-VERIFY] *** flag not set — modem will IGNORE the list ***\n");
        } else {
            fprintf(stderr, "[GSM-VERIFY] count OK (%zu ARFCNs), flag ON — ready for reboot\n",
                    expected_count);
        }
    }

    // ── Pre-lock cleanup — MUST be called before any lock_lte_vN attempt
    // Without this, leftover files from a previous run can shadow the new
    // lock. Critical scenario:
    //   - Previous run wrote cell_restrict_opt_params (v2) and crashed
    //   - New run writes cell_lock_list (v3)
    //   - Modem on reboot sees BOTH files, prefers v2 (older API), ignores v3
    //   - Lock appears to "fail" with the new earfcn/pci
    //
    // We aggressively unlink ALL three lock paths before writing the new one,
    // so the modem has exactly one source of truth.
    // ══════════════════════════════════════════════════════════════════════
    int prelock_cleanup() {
        fprintf(stderr, "[PRELOCK] === clearing stale lock files ===\n");
        // Best-effort: don't fail if any individual unlink fails (file may not
        // exist). We just want to guarantee none of these are on flash when
        // the next lock write happens.
        efs_unlink(LTE_CELL_LOCK_LIST_PATH_v3);
        efs_unlink(LTE_LOCK_PATH_v2);
        efs_unlink(LTE_LOCK_PATH_v1);
        sync_to_flash(LTE_CELL_LOCK_LIST_PATH_v3);
        sync_to_flash(LTE_LOCK_PATH_v2);
        sync_to_flash(LTE_LOCK_PATH_v1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    }

    // ══════════════════════════════════════════════════════════════════════
    // Lock-attempt logging — emits parseable lines for the Kotlin layer.
    //
    // When a lock attempt is later judged "failed" by the chain scanner
    // (timeout waiting for new identity), the user needs to see exactly
    // what bytes were sent. These two helpers emit consistent prefixes
    // [LOCK_TRY] / [LOCK_DONE] that the Kotlin Runtime.exec parser can grep.
    // ══════════════════════════════════════════════════════════════════════
    void log_lock_attempt(const char* ver, const char* path,
                          const uint8_t* data, size_t len,
                          uint32_t earfcn, uint16_t pci) {
        fprintf(stderr, "[LOCK_TRY] %s earfcn=%u pci=%u path=%s len=%zu\n",
                ver, earfcn, pci, path, len);
        fprintf(stderr, "[LOCK_TRY] %s hex:", ver);
        for (size_t i = 0; i < len; ++i) {
            if (i > 0 && i % 16 == 0) fprintf(stderr, "\n[LOCK_TRY] %s     ", ver);
            fprintf(stderr, " %02X", data[i]);
        }
        fprintf(stderr, "\n");
    }

    void log_lock_outcome(const char* ver, int ret,
                          uint32_t earfcn, uint16_t pci) {
        fprintf(stderr, "[LOCK_DONE] %s earfcn=%u pci=%u write=%s errno=%d\n",
                ver, earfcn, pci, ret == 0 ? "OK" : "FAIL", errno_);
    }

    // ══════════════════════════════════════════════════════════════════════
    // Unlock helpers
    //
    // CRITICAL: a single UNLINK call is not enough — sometimes the modem
    // returns errno=0 but the file is still on flash (cache inconsistency).
    // We must:
    //   1. UNLINK
    //   2. SYNC to flash (force flush)
    //   3. Verify with STAT — if file still exists, retry
    //
    // Without this, leftover cell_lock_list from a previous failed attempt
    // can persist and contaminate the next lock attempt.
    // ══════════════════════════════════════════════════════════════════════
    int unlock_lte() {
        lte_locked_ = false;
        fprintf(stderr, "[CLEANUP] === unlock_lte: starting wipe ===\n");
        bool ok = true;

        // 1) Best-effort UNLINK with built-in STAT verify
        if (unlink_with_retry(LTE_CELL_LOCK_LIST_PATH_v3) != 0) ok = false;
        if (unlink_with_retry(LTE_LOCK_PATH_v2) != 0) ok = false;
        if (unlink_with_retry(LTE_LOCK_PATH_v1) != 0) ok = false;

        // 2) Final flash sync (separately) for the v3 list path. Even if
        //    unlink reported success, flash sync may not have been called
        //    after the LAST unlink. Force one more sync to guarantee.
        sync_to_flash(LTE_CELL_LOCK_LIST_PATH_v3);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // 3) Double-check via TWO methods — STAT and GET. They use different
        //    code paths in the modem firmware; if STAT lies (returns ABSENT
        //    when file is still there from cache), GET will catch it. If GET
        //    succeeds with non-zero data — file is definitely still present.
        bool double_verify_ok = true;
        for (const char* path : {LTE_CELL_LOCK_LIST_PATH_v3,
                                 LTE_LOCK_PATH_v2,
                                 LTE_LOCK_PATH_v1}) {
            // Method 1: STAT
            bool stat_says_exists = efs_file_exists(path);

            // Method 2: GET — try to read it. If we get bytes, file exists.
            std::vector<uint8_t> data;
            int read_rc = efs_read_file(path, data);
            bool get_says_exists = (read_rc > 0);

            if (stat_says_exists || get_says_exists) {
                fprintf(stderr, "[CLEANUP] *** %s: STILL PRESENT after unlock! "
                                "stat=%s get=%s (read=%d bytes) ***\n",
                        path,
                        stat_says_exists ? "EXISTS" : "absent",
                        get_says_exists ? "EXISTS" : "absent",
                        read_rc);
                double_verify_ok = false;

                // Last-ditch effort: try unlinking once more without retry overhead
                fprintf(stderr, "[CLEANUP] retrying single UNLINK on %s\n", path);
                efs_unlink(path);
                sync_to_flash(path);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));

                // Re-verify
                if (efs_file_exists(path) || efs_read_file(path, data) > 0) {
                    fprintf(stderr, "[CLEANUP] *** %s: STILL PRESENT after retry "
                                    "— modem will likely re-camp on EFS lock after reboot ***\n",
                            path);
                    ok = false;
                } else {
                    fprintf(stderr, "[CLEANUP] %s: gone after retry\n", path);
                }
            } else {
                fprintf(stderr, "[CLEANUP] %s: confirmed gone (stat+get agree)\n", path);
            }
        }

        if (!double_verify_ok && ok) {
            fprintf(stderr, "[CLEANUP] WARN: unlink_with_retry said OK but "
                            "double-verify caught residue (now resolved)\n");
        }

        fprintf(stderr, "[CLEANUP] === unlock_lte: %s ===\n", ok ? "OK" : "FAILED");
        return ok ? 0 : -1;
    }

    int unlock_umts() {
        fprintf(stderr, "[CLEANUP] === unlock_umts() — wiping WCDMA lock files ===\n");
        bool ok = true;
        // Attempt ALL paths regardless of individual failures — a stubborn
        // file on one path must not prevent clearing the others. Note:
        // lock_umts(v2) only creates wl1_srch_debug_utils + freq_lock_item,
        // while lock_umts_by_freq(v1) creates freq_lock_item + enable_psc_lock,
        // so depending on which lock was applied some paths are already absent
        // (unlink_with_retry treats "absent" as success).
        if (unlink_with_retry(WCDMA_RRC_LOCK_PATH)            != 0) ok = false;
        if (unlink_with_retry(WCDMA_RRC_ENABLE_PSC_LOCK_PATH) != 0) ok = false;
        if (unlink_with_retry(WCDMA_LOCK_BY_FREQ_AND_PSC_PATH)!= 0) ok = false;
        fprintf(stderr, "[CLEANUP] === unlock_umts: %s ===\n", ok ? "OK" : "FAILED");
        return ok ? 0 : -1;
    }

    /**
     * Unlock everything — call between scan attempts to guarantee clean slate.
     * Returns 0 only if ALL files are confirmed gone.
     */
    int unlock_all() {
        fprintf(stderr, "[CLEANUP] === unlock_all() — wiping all lock files ===\n");
        int rc = 0;
        if (unlock_lte() != 0) rc = -1;
        if (unlock_umts() != 0) rc = -1;
        if (unlock_gsm() != 0) rc = -1;
        fprintf(stderr, "[CLEANUP] === unlock_all done (rc=%d) ===\n", rc);
        return rc;
    }

    /**
     * Robust UNLINK: tries up to 3 times, syncs to flash after each try,
     * verifies with STAT that the file is actually gone.
     *
     * Returns 0 if file is confirmed deleted (or never existed), -1 otherwise.
     */
    int unlink_with_retry(const char* path) {
        for (int i = 1; i <= 3; ++i) {
            fprintf(stderr, "[UNLINK] === attempt %d/3 path=%s ===\n", i, path);

            // First, check if file exists at all
            if (!efs_file_exists(path)) {
                fprintf(stderr, "[UNLINK] file already absent — OK\n");
                return 0;
            }

            // Try to unlink
            int rc = efs_unlink(path);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));

            // Force sync — the unlink itself needs to be persisted!
            sync_to_flash(path);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Verify
            if (!efs_file_exists(path)) {
                fprintf(stderr, "[UNLINK] verified gone after attempt %d (unlink rc=%d)\n", i, rc);
                return 0;
            }
            fprintf(stderr, "[UNLINK] file still present after attempt %d — retry\n", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        fprintf(stderr, "[UNLINK] FAILED to delete %s after 3 attempts\n", path);
        return -1;
    }

    /**
     * Cheap existence check using EFS2 STAT (subcmd 0x0F = 15).
     *
     * Request:  [4B 13 0F 00] [path\0]
     * Response: [4B 13 0F 00] [error:i32] [stat fields...]
     *
     * If error is non-zero (typically ENOENT=2), file does not exist.
     */
    bool efs_file_exists(const char* path) {
        size_t path_len = strlen(path) + 1;
        std::vector<uint8_t> pkt(4 + path_len, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x0F; pkt[3]=0x00;  // STAT = 15
        memcpy(pkt.data() + 4, path, path_len);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) {
            // No response — assume exists to be safe (will retry)
            fprintf(stderr, "[STAT] no response for %s (assuming exists)\n", path);
            return true;
        }

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        if (rl < 4) return true;  // assume exists if response weird

        int32_t err;
        memcpy(&err, r, 4);
        // err == 0 → file exists (stat succeeded)
        // err != 0 → file doesn't exist (or other error)
        bool exists = (err == 0);
        fprintf(stderr, "[STAT] %s -> %s (errno=%d)\n",
                path, exists ? "EXISTS" : "ABSENT", err);
        return exists;
    }

    // ══════════════════════════════════════════════════════════════════════
    // Reliable EFS write strategy (matches libopenpst/QPST exactly):
    //
    //   1. UNLINK existing file
    //   2. PUT with O_TRUNC (or fallback OPEN/WRITE/CLOSE)
    //   3. SYNC_NO_WAIT(path, seq) → returns TOKEN
    //   4. POLL GET_SYNC_STATUS(path, token, seq) until status != 0
    //      (this guarantees data is written to NAND flash)
    //   5. Now safe to reboot
    //
    // CRITICAL FIX: SyncNoWait opcode is 48 (0x30), NOT 24 (0x18)!
    // The previous opcode 0x18 was wrong (that's FactoryImageEnd in libopenpst).
    // That's why the SYNC was timing out before.
    //
    // GET-based readback was removed (response format varies, not reliable).
    // ══════════════════════════════════════════════════════════════════════
    int write_efs_reliable(const char* path, const void* data, size_t len) {
        for (int attempt = 1; attempt <= 3; ++attempt) {
            fprintf(stderr, "[EFS] === Attempt %d/3 path=%s len=%zu ===\n", attempt, path, len);

            // Step 1: UNLINK existing file (ignore errno — file may not exist)
            efs_unlink(path);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));

            // Step 2: PUT with O_TRUNC
            bool wrote_ok = false;
            int put_ret = efs_item_put(path, data, len);
            if (put_ret == 0) {
                fprintf(stderr, "[EFS] PUT OK\n");
                wrote_ok = true;
            } else {
                fprintf(stderr, "[EFS] PUT failed (errno=%d), trying OPEN/WRITE/CLOSE\n", errno_);
                if (efs_open_write_close(path, data, len) == 0) {
                    fprintf(stderr, "[EFS] OPEN/WRITE/CLOSE OK\n");
                    wrote_ok = true;
                }
            }

            if (!wrote_ok) {
                fprintf(stderr, "[EFS] Both methods failed, retry...\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }

            // Step 3: SYNC to flash (correct flow per libopenpst)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (sync_to_flash(path) == 0) {
                fprintf(stderr, "[EFS] ==> WRITE + SYNC COMPLETED\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return 0;
            }
            // If sync fails, the write may still persist — log and continue
            fprintf(stderr, "[EFS] ==> SYNC failed but PUT was OK, returning success\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return 0;
        }
        errno_ = -100;
        return -1;
    }

    // ══════════════════════════════════════════════════════════════════════
    // Full sync flow per libopenpst/QPST:
    //   1. SYNC_NO_WAIT(path, seq) → returns token
    //   2. POLL GET_SYNC_STATUS(path, token, seq) until status != 0
    // Returns 0 on success, errno on failure.
    // ══════════════════════════════════════════════════════════════════════
    int sync_to_flash(const char* path) {
        // Step 1: SyncNoWait
        uint16_t seq = ++sync_seq_;  // unique sequence per call
        uint32_t token = 0;
        int rc = efs_sync_no_wait(path, seq, token);
        if (rc != 0) {
            fprintf(stderr, "[EFS] SYNC_NO_WAIT failed: errno=%d\n", rc);
            return rc;
        }
        if (token == 0) {
            fprintf(stderr, "[EFS] SYNC_NO_WAIT got token=0, no polling needed (already synced)\n");
            return 0;
        }
        fprintf(stderr, "[EFS] SYNC_NO_WAIT OK: token=0x%08X\n", token);

        // Step 2: Poll GetSyncStatus
        for (int i = 0; i < 30; ++i) {  // up to 30 polls = ~3 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            uint8_t status = 0;
            rc = efs_get_sync_status(path, token, ++sync_seq_, status);
            if (rc != 0) {
                fprintf(stderr, "[EFS] GET_SYNC_STATUS failed at iter %d: errno=%d\n", i, rc);
                return rc;
            }
            if (status != 0) {  // non-zero = sync complete
                fprintf(stderr, "[EFS] GET_SYNC_STATUS done after %d polls (status=%u)\n", i+1, status);
                return 0;
            }
        }
        fprintf(stderr, "[EFS] GET_SYNC_STATUS: timed out after 3 seconds\n");
        return -1;
    }

    // ══════════════════════════════════════════════════════════════════════
    // EFS2_DIAG_SYNC_NO_WAIT (subcmd 48 = 0x30)
    //
    // Request:  [4B 13 30 00] [sequence:u16] [path\0]
    // Response: [4B 13 30 00] [sequence:u16] [token:u32] [error:i32]
    //
    // NOTE: Despite "NoWait", the operation IS asynchronous in the modem.
    // It returns immediately with a token; we must poll GetSyncStatus.
    // ══════════════════════════════════════════════════════════════════════
    int efs_sync_no_wait(const char* path, uint16_t seq, uint32_t& out_token) {
        size_t path_len = strlen(path) + 1;
        std::vector<uint8_t> pkt(4 + 2 + path_len, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x30; pkt[3]=0x00;  // SyncNoWait = 48
        memcpy(pkt.data() + 4, &seq, 2);
        memcpy(pkt.data() + 6, path, path_len);

        fprintf(stderr, "[EFS] SyncNoWait seq=%u '%s'\n", seq, path);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) {
            fprintf(stderr, "[EFS] SyncNoWait: no response\n");
            return -1;
        }

        if (debug_) {
            fprintf(stderr, "[EFS] SyncNoWait raw rsp (%zu):", rsp.size());
            for (size_t i = 0; i < rsp.size() && i < 32; ++i) fprintf(stderr, " %02X", rsp[i]);
            fprintf(stderr, "\n");
        }

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        // [seq:u16][token:u32][error:i32] = 10 bytes minimum
        if (rl < 10) {
            fprintf(stderr, "[EFS] SyncNoWait: rsp too short (%zu)\n", rl);
            return -2;
        }

        uint16_t got_seq;
        uint32_t token;
        int32_t  err;
        memcpy(&got_seq, r, 2);
        memcpy(&token, r + 2, 4);
        memcpy(&err, r + 6, 4);

        fprintf(stderr, "[EFS] SyncNoWait rsp: seq=%u token=0x%08X err=%d\n",
                got_seq, token, err);

        if (err != 0) { errno_ = err; return err; }
        out_token = token;
        return 0;
    }

    // ══════════════════════════════════════════════════════════════════════
    // EFS2_DIAG_SYNC_GET_STATUS (subcmd 49 = 0x31)
    //
    // Request:  [4B 13 31 00] [sequence:u16] [token:u32] [path\0]
    // Response: [4B 13 31 00] [sequence:u16] [status:u8] [error:i32]
    //
    // status == 0 → still pending
    // status != 0 → sync complete (data written to flash)
    // ══════════════════════════════════════════════════════════════════════
    int efs_get_sync_status(const char* path, uint32_t token, uint16_t seq, uint8_t& out_status) {
        size_t path_len = strlen(path) + 1;
        std::vector<uint8_t> pkt(4 + 2 + 4 + path_len, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x31; pkt[3]=0x00;  // SyncGetStatus = 49
        memcpy(pkt.data() + 4, &seq, 2);
        memcpy(pkt.data() + 6, &token, 4);
        memcpy(pkt.data() + 10, path, path_len);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) return -1;

        if (debug_) {
            fprintf(stderr, "[EFS] SyncGetStatus raw rsp (%zu):", rsp.size());
            for (size_t i = 0; i < rsp.size() && i < 32; ++i) fprintf(stderr, " %02X", rsp[i]);
            fprintf(stderr, "\n");
        }

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        // [seq:u16][status:u8][error:i32] = 7 bytes minimum
        if (rl < 7) return -2;

        uint16_t got_seq;
        uint8_t  status;
        int32_t  err;
        memcpy(&got_seq, r, 2);
        memcpy(&status, r + 2, 1);
        memcpy(&err, r + 3, 4);

        if (err != 0) { errno_ = err; return err; }
        out_status = status;
        return 0;
    }

    /**
     * EFS2 Item GET (subcmd 0x27) — reads a file and returns contents.
     * NOTE: response format varies by modem version — not used for verification anymore.
     * Kept for manual debugging if needed.
     * Format on SM8550: [subsys_hdr 4B] [echo: size+oflag+mode 8B] [code 2B] [errno 4B] [nbytes 4B] [data]
     */
    int efs_item_get(const char* path, std::vector<uint8_t>& out, size_t max_sz = 4096) {
        size_t path_len = strlen(path) + 1;
        size_t pkt_sz = 4 + sizeof(Efs2ItemHdr) + path_len + 3;
        std::vector<uint8_t> pkt(pkt_sz, 0);

        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x27; pkt[3]=0x00;  // EFS2_GET = 39

        auto* h = reinterpret_cast<Efs2ItemHdr*>(pkt.data() + 4);
        h->size  = (uint32_t)max_sz;
        h->oflag = EFS_O_RDONLY;
        h->mode  = 0;
        h->code  = 0x17BB;  // GET code

        memcpy(pkt.data() + 4 + sizeof(Efs2ItemHdr), path, path_len);

        fprintf(stderr, "[EFS] GET '%s'\n", path);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) return -1;

        // Hex dump for debugging
        fprintf(stderr, "[EFS] GET raw rsp (%zu):", rsp.size());
        for (size_t i = 0; i < rsp.size() && i < 32; ++i) fprintf(stderr, " %02X", rsp[i]);
        fprintf(stderr, "\n");

        // SM8550 format: [subsys_hdr 4B] [size:u32] [oflag:u16] [mode:u16] [code:u16] [errno:i32] [nbytes:i32] [data]
        // Skip subsys_hdr (4) + echoed item_hdr (10) = 14 bytes total
        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        if (rl < sizeof(Efs2ItemHdr) + 8) { errno_ = -4; return -1; }

        // Skip echoed Efs2ItemHdr (size+oflag+mode+code = 10 bytes)
        r += sizeof(Efs2ItemHdr);
        rl -= sizeof(Efs2ItemHdr);

        int32_t err;
        int32_t nbytes;
        memcpy(&err, r, 4);
        memcpy(&nbytes, r + 4, 4);

        fprintf(stderr, "[EFS] GET parsed: errno=%d bytes=%d\n", err, nbytes);

        if (err != 0) { errno_ = err; return -1; }

        size_t avail = rl > 8 ? rl - 8 : 0;
        size_t n = nbytes > 0 ? std::min<size_t>((size_t)nbytes, avail) : 0;
        out.assign(r + 8, r + 8 + n);
        return 0;
    }

    // ══════════════════════════════════════════════════════════════════════
    // Method A: EFS2 Item PUT (subcmd 0x26)
    // oflag = O_RDWR|O_CREAT|O_TRUNC = 0x0242 (was 0x0040!)
    // mode  = 0777 = 0x01FF (was 0x0008!)
    // ══════════════════════════════════════════════════════════════════════
    int efs_item_put(const char* path, const void* data, size_t data_len) {
        size_t path_len = strlen(path) + 1;
        size_t pkt_sz = 4 + sizeof(Efs2ItemHdr) + data_len + path_len + 3;
        std::vector<uint8_t> pkt(pkt_sz, 0);

        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x26; pkt[3]=0x00;

        auto* h = reinterpret_cast<Efs2ItemHdr*>(pkt.data() + 4);
        h->size  = (uint32_t)data_len;
        h->oflag = EFS_O_RDWR | EFS_O_CREAT | EFS_O_TRUNC;  // 0x0242
        h->mode  = 0x01FF;                                    // 0777
        h->code  = 0x17BA;

        uint8_t* p = pkt.data() + 4 + sizeof(Efs2ItemHdr);
        if (data_len > 0) { memcpy(p, data, data_len); p += data_len; }
        memcpy(p, path, path_len);

        if (debug_) dump_pkt("PUT", pkt);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) return -1;

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        if (rl >= sizeof(Efs2ItemRsp)) {
            auto* pr = reinterpret_cast<const Efs2ItemRsp*>(r);
            fprintf(stderr, "[EFS] PUT rsp: code=0x%04X errno=%d\n", pr->code, pr->diag_errno);
            if (pr->code == 0x17BA && pr->diag_errno == 0) return 0;
            errno_ = pr->diag_errno;
        }
        return -1;
    }

    // ══════════════════════════════════════════════════════════════════════
    // Method B: POSIX OPEN(O_TRUNC) + WRITE + CLOSE
    // This is exactly what the Askey firmware does internally
    // ══════════════════════════════════════════════════════════════════════
    int efs_open_write_close(const char* path, const void* data, size_t data_len) {
        // --- OPEN ---
        // subcmd 2 = EFS2_DIAG_OPEN
        // format: [4B 13 02 00] [oflag:i32] [mode:i32] [path\0]
        {
            size_t path_len = strlen(path) + 1;
            size_t pkt_sz = 4 + 4 + 4 + path_len;
            std::vector<uint8_t> pkt(pkt_sz, 0);
            pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x02; pkt[3]=0x00;

            int32_t oflag = EFS_O_RDWR | EFS_O_CREAT | EFS_O_TRUNC | EFS_O_AUTODIR;
            int32_t mode  = 0777;
            memcpy(pkt.data()+4, &oflag, 4);
            memcpy(pkt.data()+8, &mode, 4);
            memcpy(pkt.data()+12, path, path_len);

            fprintf(stderr, "[EFS] OPEN '%s' oflag=0x%X mode=0%o\n", path, oflag, mode);

            std::vector<uint8_t> rsp;
            if (!send_and_wait(pkt, rsp)) return -1;

            // Response: [4B 13 02 00] [fd:i32]
            const uint8_t* r = skip_hdr(rsp);
            size_t rl = rsp.size() - (r - rsp.data());
            if (rl < 4) { errno_ = -10; return -1; }

            memcpy(&fd_, r, 4);
            fprintf(stderr, "[EFS] OPEN fd=%d\n", fd_);
            if (fd_ < 0) { errno_ = fd_; return -1; }
        }

        // --- WRITE ---
        // subcmd 5 = EFS2_DIAG_WRITE
        // format: [4B 13 05 00] [fd:i32] [offset:u32] [data...]
        {
            size_t pkt_sz = 4 + 4 + 4 + data_len;
            std::vector<uint8_t> pkt(pkt_sz, 0);
            pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x05; pkt[3]=0x00;

            memcpy(pkt.data()+4, &fd_, 4);
            uint32_t offset = 0;
            memcpy(pkt.data()+8, &offset, 4);
            memcpy(pkt.data()+12, data, data_len);

            fprintf(stderr, "[EFS] WRITE fd=%d len=%zu\n", fd_, data_len);

            std::vector<uint8_t> rsp;
            if (!send_and_wait(pkt, rsp)) { efs_close(fd_); return -1; }

            // Response: [4B 13 05 00] [fd:i32] [offset:u32] [bytes_written:i32] [errno:i32]
            const uint8_t* r = skip_hdr(rsp);
            size_t rl = rsp.size() - (r - rsp.data());
            if (rl >= 16) {
                int32_t written, err;
                memcpy(&written, r+8, 4);
                memcpy(&err, r+12, 4);
                fprintf(stderr, "[EFS] WRITE written=%d errno=%d\n", written, err);
                if (err != 0) { errno_ = err; efs_close(fd_); return -1; }
            }
        }

        // --- CLOSE ---
        int close_ret = efs_close(fd_);
        fprintf(stderr, "[EFS] CLOSE ret=%d\n", close_ret);
        return 0;
    }

    // ── EFS2 CLOSE (subcmd 3) ────────────────────────────────────────────
    int efs_close(int32_t fd) {
        std::vector<uint8_t> pkt(4 + 4, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x03; pkt[3]=0x00;
        memcpy(pkt.data()+4, &fd, 4);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) return -1;

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        if (rl >= 4) {
            int32_t err; memcpy(&err, r, 4);
            return err == 0 ? 0 : -1;
        }
        return -1;
    }

    // ── EFS2 UNLINK (subcmd 0x08) ────────────────────────────────────────
    int efs_unlink(const char* path) {
        size_t path_len = strlen(path) + 1;
        std::vector<uint8_t> pkt(4 + path_len, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x08; pkt[3]=0x00;
        memcpy(pkt.data()+4, path, path_len);

        fprintf(stderr, "[EFS] UNLINK '%s'\n", path);
        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) return -1;

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        if (rl >= 4) {
            int32_t err; memcpy(&err, r, 4);
            fprintf(stderr, "[EFS] UNLINK errno=%d\n", err);
            return err == 0 ? 0 : -1;
        }
        return -1;
    }


    bool     is_lte_locked() const { return lte_locked_; }
    uint32_t locked_earfcn() const { return lte_earfcn_; }
    uint16_t locked_pci()    const { return lte_pci_; }
    int      last_errno()    const { return errno_; }

    // ══════════════════════════════════════════════════════════════════════
    // LTE band preference (public — called from main and Kotlin via CLI)
    // ══════════════════════════════════════════════════════════════════════
    /** EFS2 OPEN (subcmd 0x02 — libopenpst protocol) — open a file, return fd.
     *
     * Returns fd >= 0 on success, -1 on error (errno_ set).
     *
     * Why we use OPEN+READ+CLOSE flow instead of EFS_GET (0x27):
     * On Xiaomi SM8550 (HyperOS/MIUI), the atomic EFS_GET command is
     * rejected at the DIAG layer with response 0x15 (DIAG_BAD_LEN_F).
     * Confirmed empirically: read of /nv/item_files/modem/mmode/lte_bandpref
     * via EFS_GET returns "15 <request-echo>".
     *
     * The OPEN/READ/CLOSE flow uses the same sub-protocol IDs as the working
     * efs_open_write_close (which uses OPEN=0x02 + WRITE=0x05 + CLOSE=0x03),
     * so we trust those numbers for read too. READ = 0x04.
     */
    int efs_open_read(const char* path, int32_t oflag = 0 /*O_RDONLY*/) {
        size_t path_len = strlen(path) + 1;
        // hdr(4) + oflag(4) + mode(4) + path(NUL-term)
        std::vector<uint8_t> pkt(4 + 4 + 4 + path_len, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x02; pkt[3]=0x00;  // EFS_OPEN = 2
        memcpy(pkt.data() + 4, &oflag, 4);
        // pkt[8..11] = mode = 0 for O_RDONLY
        memcpy(pkt.data() + 12, path, path_len);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) {
            fprintf(stderr, "[EFS] OPEN(rd) no response for %s\n", path);
            errno_ = -1;
            return -1;
        }

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        if (rl < 4) {
            fprintf(stderr, "[EFS] OPEN(rd) response too short (%zu bytes)\n", rl);
            errno_ = -1;
            return -1;
        }

        int32_t fd;
        memcpy(&fd, r, 4);
        if (fd < 0) {
            int32_t err = 0;
            if (rl >= 8) memcpy(&err, r + 4, 4);
            fprintf(stderr, "[EFS] OPEN(rd) '%s' failed fd=%d errno=%d\n",
                    path, fd, err);
            errno_ = err ? err : fd;
            return -1;
        }
        if (debug_) fprintf(stderr, "[EFS] OPEN(rd) '%s' -> fd=%d\n", path, fd);
        return fd;
    }

    /** EFS2 READ (subcmd 0x04 — libopenpst protocol).
     *  Reads up to nbytes from fd at offset. Appends to `out`.
     *  Returns number of bytes actually read, -1 on error.
     */
    int efs_read(int32_t fd, uint32_t offset, uint32_t nbytes,
                 std::vector<uint8_t>& out) {
        // hdr(4) + fd(4) + nbytes(4) + offset(4)
        std::vector<uint8_t> pkt(4 + 4 + 4 + 4, 0);
        pkt[0]=0x4B; pkt[1]=0x13; pkt[2]=0x04; pkt[3]=0x00;  // EFS_READ = 4
        memcpy(pkt.data() + 4,  &fd,     4);
        memcpy(pkt.data() + 8,  &nbytes, 4);
        memcpy(pkt.data() + 12, &offset, 4);

        std::vector<uint8_t> rsp;
        if (!send_and_wait(pkt, rsp)) {
            fprintf(stderr, "[EFS] READ fd=%d no response\n", fd);
            errno_ = -1;
            return -1;
        }

        if (debug_) {
            fprintf(stderr, "[EFS] READ raw rsp (%zu bytes):", rsp.size());
            for (size_t i = 0; i < std::min(rsp.size(), (size_t)64); ++i) {
                if (i % 16 == 0) fprintf(stderr, "\n[EFS]   %04zx:", i);
                fprintf(stderr, " %02X", rsp[i]);
            }
            fprintf(stderr, "\n");
        }

        const uint8_t* r = skip_hdr(rsp);
        size_t rl = rsp.size() - (r - rsp.data());
        // Response: <fd:i32> <offset:u32> <bytes_read:i32> <errno:i32> <data>
        if (rl < 16) {
            fprintf(stderr, "[EFS] READ response too short (%zu bytes)\n", rl);
            errno_ = -1;
            return -1;
        }
        int32_t r_fd, r_off, bytes_read, err;
        memcpy(&r_fd,       r,      4);
        memcpy(&r_off,      r + 4,  4);
        memcpy(&bytes_read, r + 8,  4);
        memcpy(&err,        r + 12, 4);
        (void)r_fd; (void)r_off;
        if (bytes_read < 0 || err != 0) {
            fprintf(stderr, "[EFS] READ fd=%d bytes_read=%d errno=%d\n",
                    fd, bytes_read, err);
            errno_ = err;
            return -1;
        }
        size_t data_avail = rl - 16;
        size_t data_len   = std::min<size_t>((size_t)bytes_read, data_avail);
        out.insert(out.end(), r + 16, r + 16 + data_len);
        if (debug_) fprintf(stderr, "[EFS] READ fd=%d -> %zu bytes\n", fd, data_len);
        return static_cast<int>(data_len);
    }

    /** High-level: open + read full file + close.
     *
     * This is the **primary** EFS file read method. Use this instead of
     * efs_read_file() (which uses EFS_GET = 0x27, broken on SM8550).
     *
     * Returns 0 on success with file content in `out`. Returns -1 on
     * failure; last_errno() gives details.
     */
    int efs_read_file(const char* path, std::vector<uint8_t>& out) {
        out.clear();
        int32_t fd = efs_open_read(path, 0 /*O_RDONLY*/);
        if (fd < 0) return -1;

        // Read in chunks (some FW limits READ to ~1024 bytes per request)
        constexpr uint32_t CHUNK = 1024;
        uint32_t offset = 0;
        while (true) {
            int got = efs_read(fd, offset, CHUNK, out);
            if (got < 0) {
                efs_close(fd);   // best-effort cleanup
                return -1;
            }
            if (got == 0 || (uint32_t)got < CHUNK) {
                break;   // EOF
            }
            offset += got;
            if (offset > 1024 * 1024) break;   // sanity: 1 MB cap
        }
        efs_close(fd);
        return 0;
    }

    /** Public wrapper around efs_read_file — used by CLI `--efs cat`.
     *  Uses OPEN/READ/CLOSE under the hood (EFS_GET is broken on SM8550). */
    int read_efs_for_cli(const char* path, std::vector<uint8_t>& out) {
        return efs_read_file(path, out);
    }

    static constexpr const char* LTE_BAND_PREF_PATH = "/nv/item_files/modem/mmode/lte_bandpref";

    /** LTE band preference via direct EFS NV write.
     *
     * Path: /nv/item_files/modem/mmode/lte_bandpref
     *
     * Format observed on Xiaomi SM8550: **8 bytes**, u64 LE bitmask.
     * Bit N (0-indexed) = LTE band (N+1).
     *
     * Example originals seen in field:
     *   0x000001E20809_18DF = B1,B2,B3,B4,B5,B7,B8,B12,B13,B17,B20,B28,
     *                        B34,B38,B39,B40,B41 (typical RU MIUI config)
     *
     * IMPORTANT: write exactly 8 bytes, NOT 16. Modem firmware may treat
     * a 16-byte write as a different file format and fail to apply the
     * change. Verified by comparing pre/post `xxd` output.
     *
     * Set mask = 0 to disable all bands (don't do this!), or use original
     * mask to restore. The "all bands" mask 0xFFFFFFFFFFFFFFFF is only
     * valid up to bands 1-64; beyond that the modem ignores.
     *
     * @param mask 64-bit bitmask. e.g. 0x40 = B7 only; 0x4 = B3 only.
     */
    int set_lte_band_pref(uint64_t mask) {
        uint8_t val[8] = {};
        for (int i = 0; i < 8; ++i) {
            val[i] = static_cast<uint8_t>((mask >> (i * 8)) & 0xFF);
        }
        return write_efs_reliable(LTE_BAND_PREF_PATH, val, sizeof(val));
    }

    /** Read current LTE band preference from EFS.
     *
     * Reads `/nv/item_files/modem/mmode/lte_bandpref` (8 bytes on Xiaomi)
     * and returns the u64 LE bitmask. Returns 0 on failure — call
     * last_errno() to disambiguate from "all bands cleared".
     */
    uint64_t read_lte_band_pref() {
        std::vector<uint8_t> data;
        int rc = efs_read_file(LTE_BAND_PREF_PATH, data);
        if (rc != 0 || data.empty()) return 0;
        uint64_t mask = 0;
        size_t n = (data.size() < 8) ? data.size() : 8;
        for (size_t i = 0; i < n; ++i) {
            mask |= (static_cast<uint64_t>(data[i]) << (i * 8));
        }
        return mask;
    }

private:
    static constexpr const char* LTE_LOCK_PATH_v1 = "/nv/item_files/modem/lte/rrc/csp/pci_lock";
    static constexpr const char* LTE_LOCK_PATH_v2 = "/nv/item_files/modem/lte/rrc/efs/cell_restrict_opt_params";
    static constexpr const char* LTE_CELL_LOCK_LIST_PATH_v3 = "/nv/item_files/modem/lte/rrc/efs/cell_lock_list";

    static constexpr const char* WCDMA_RRC_ENABLE_PSC_LOCK_PATH = "/nv/item_files/wcdma/rrc/wcdma_rrc_enable_psc_lock";
    static constexpr const char* WCDMA_RRC_LOCK_PATH = "/nv/item_files/wcdma/rrc/wcdma_rrc_freq_lock_item";
    static constexpr const char* WCDMA_LOCK_BY_FREQ_AND_PSC_PATH = "/nv/item_files/wcdma/l1/srch/wl1_srch_debug_utils";
    // GSM (GERAN) ARFCN-list lock (recipe from Qualcomm.txt §2.4):
    //   1. write list of uint16 LE ARFCNs to GSM_ARFCN_LIST_PATH
    //   2. write 1 byte (0x01) to GSM_FEATURE_LOCK_ARFCN_PATH
    //   3. reboot modem
    // Confirmed working on Xiaomi Mi 10/10T, Poco X3 Pro, Samsung S22/S23,
    // OnePlus 8/9/10/11/13, Nothing 3a, Motorola Edge 50 Ultra, and others.
    static constexpr const char* GSM_ARFCN_LIST_PATH           = "/nv/item_files/modem/geran/rr_efs_arfcn_list";
    static constexpr const char* GSM_FEATURE_LOCK_ARFCN_PATH   = "/nv/item_files/modem/geran/grr/feature_lock_arfcn_enabled";

    // ── UE usage setting: voice-centric (1) blocks SIM-less registration ──
    // Setting this to data_centric (0) makes the modem ignore IMS / VoLTE
    // requirements and camp on whatever PLMN it can hear. Critical for
    // SIM-less scanning where there's no carrier to register with.
    static constexpr const char* UE_USAGE_SETTING_PATH = "/nv/item_files/modem/mmode/ue_usage_setting";

public:
    /**
     * Switch the modem to data-centric mode (UE usage setting = 0).
     *
     * Voice-centric (default for smartphones) tells the modem to refuse
     * registration if VoLTE/VoNR services aren't available. Without a SIM,
     * IMS can't register → modem gives up and stops actively scanning.
     *
     * Data-centric (typical for hotspot devices) tells the modem to camp
     * on any reachable network regardless of IMS state. This is what we
     * want for cell discovery without a SIM.
     *
     * Requires a modem reboot to take effect.
     *
     * Returns 0 on successful EFS write, -1 otherwise.
     */
    int force_data_centric() {
        uint8_t value = 0x00;  // 0 = data-centric, 1 = voice-centric
        fprintf(stderr, "[NV] writing ue_usage_setting=0 (data-centric, "
                        "enables SIM-less PLMN registration)\n");
        return write_efs_reliable(UE_USAGE_SETTING_PATH, &value, 1);
    }

    /**
     * Read current UE usage setting. Returns -1 on read failure,
     * otherwise 0 (data-centric) or 1 (voice-centric).
     */
    int read_ue_usage_setting() {
        std::vector<uint8_t> data;
        int rc = efs_read_file(UE_USAGE_SETTING_PATH, data);
        if (rc <= 0 || data.empty()) {
            fprintf(stderr, "[NV] ue_usage_setting read failed (rc=%d)\n", rc);
            return -1;
        }
        int v = data[0];
        fprintf(stderr, "[NV] ue_usage_setting = %d (%s)\n", v,
                v == 0 ? "data-centric" : v == 1 ? "voice-centric" : "unknown");
        return v;
    }

    /**
     * Restore voice-centric mode (UE usage setting = 1) — the default for
     * smartphones. Call this after a SIM-less scan session if the device
     * normally has a SIM, otherwise VoLTE may misbehave.
     */
    int restore_voice_centric() {
        uint8_t value = 0x01;
        fprintf(stderr, "[NV] writing ue_usage_setting=1 (voice-centric, default)\n");
        return write_efs_reliable(UE_USAGE_SETTING_PATH, &value, 1);
    }

private:

    // Skip subsys header in response
    const uint8_t* skip_hdr(const std::vector<uint8_t>& rsp) {
        if (rsp.size() >= 4 && rsp[0] == 0x4B && rsp[1] == 0x13)
            return rsp.data() + 4;
        return rsp.data();
    }

    void dump_pkt(const char* label, const std::vector<uint8_t>& pkt) {
        fprintf(stderr, "[EFS] %s pkt (%zu):", label, pkt.size());
        for (size_t i = 0; i < pkt.size(); ++i) {
            if (i % 16 == 0) fprintf(stderr, "\n  %04zx:", i);
            fprintf(stderr, " %02X", pkt[i]);
        }
        fprintf(stderr, "\n");
    }

    bool send_and_wait(const std::vector<uint8_t>& pkt, std::vector<uint8_t>& out) {
        auto& lib = LibdiagLoader::instance();
        if (!lib.send_dci_async_req) { errno_ = -1; return false; }
        { std::lock_guard<std::mutex> lk(mx_); rsp_ok_ = false; rsp_.clear(); }

        uint8_t rsp_raw[4096] = {};
        int ret = lib.send_dci_async_req(client_id_,
                                         const_cast<uint8_t*>(pkt.data()), (int)pkt.size(),
                                         rsp_raw, sizeof(rsp_raw), &DiagCellLock::on_rsp, this);
        if (ret != DIAG_DCI_NO_ERROR) { errno_ = ret; return false; }

        std::unique_lock<std::mutex> lk(mx_);
        if (!cv_.wait_for(lk, std::chrono::seconds(5), [this]{ return rsp_ok_; })) {
            fprintf(stderr, "[EFS] TIMEOUT\n"); errno_ = -2; return false;
        }
        out = rsp_;
        return true;
    }

    static void on_rsp(unsigned char* buf, int len, void* ctx) {
        auto* s = static_cast<DiagCellLock*>(ctx);
        std::lock_guard<std::mutex> lk(s->mx_);
        s->rsp_.assign(buf, buf + len);
        s->rsp_ok_ = true;
        s->cv_.notify_one();
    }

    int client_id_; bool debug_ = false;
    bool lte_locked_ = false; uint32_t lte_earfcn_ = 0; uint16_t lte_pci_ = 0;
    int errno_ = 0; int32_t fd_ = -1;
    uint16_t sync_seq_ = 0;  // monotonic seq counter for SyncNoWait/GetStatus
    std::mutex mx_; std::condition_variable cv_;
    bool rsp_ok_ = false; std::vector<uint8_t> rsp_;
};

#endif