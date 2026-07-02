/*
 * main.cpp — Qualcomm DIAG Scanner (updated with NR + identity fields)
 *
 * Shows: RAT, ARFCN, PCI/PSC/BSIC, RSRP/RSCP/RxLev, TAC/LAC, CID, MCC-MNC
 */

#include "diag_cell_lock.h"
#include "diag_dci_client.h"
// EFS2 browse (optional, needs more debugging)
// #include "diag_efs2.h"
#include "diag_common.h"
#include "gsm_cell_metrics.h"
#include "journal.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef USE_ASN1C_LTE
#include "asn1c_lte_bridge.h"
#endif
// ─────────────────────────────────────────────────────────────────────────────
// ANSI escape helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool g_color = false;

#define CLR(x) (g_color ? (x) : "")
#define RESET CLR("\033[0m")
#define BOLD CLR("\033[1m")
#define DIM CLR("\033[2m")
#define RED CLR("\033[31m")
#define GREEN CLR("\033[32m")
#define YELLOW CLR("\033[33m")
#define CYAN CLR("\033[36m")
#define MAGENTA CLR("\033[35m")
#define BLUE CLR("\033[34m")
#define WHITE CLR("\033[97m")
#define CLS CLR("\033[2J\033[H")
#define CLRLINE CLR("\033[K")

// ─────────────────────────────────────────────────────────────────────────────
static std::atomic<bool> g_running{true};

// Globals for emergency lock cleanup on signal — see install_signal_handlers().
// When SIGINT/SIGTERM arrives mid-scan, we MUST unlink any active lock files
// otherwise the modem retains the lock across reboots and the user is stuck
// camped on a single cell until they reflash.
static DiagCellLock *g_locker_for_cleanup = nullptr;
static std::atomic<bool> g_lock_applied{false};

static void on_signal(int sig) {
    g_running = false;
    // Best-effort emergency cleanup. We can't use stdio mutexes here (async-
    // signal-unsafe) but write(2) on stderr is safe, and our locker.unlock_lte
    // calls libdiag which is technically not signal-safe — but if we just
    // crashed the alternative is leaving the modem locked forever, so it's
    // the lesser evil. Best effort.
    if (g_lock_applied.load() && g_locker_for_cleanup) {
        const char msg[] = "\n[SIGNAL] performing emergency cell-lock cleanup...\n";
        write(2, msg, sizeof(msg) - 1);
        g_locker_for_cleanup->unlock_lte();
        g_lock_applied.store(false);
    }
    (void) sig;
}

// ─────────────────────────────────────────────────────────────────────────────
struct Options {
    int duration_sec = 0;
    int refresh_sec = 3;
    bool verbose = false;
    bool nas_mode = false;
    bool no_screen = false;
    std::string json_path;
    std::string journal_path;// --journal: NDJSON packet journal (1 line/frame)
    std::string log_file;    // --log-file: tee all stderr diagnostics here too

    bool rat_gsm = true;
    bool rat_wcdma = true;
    bool rat_lte = true;
    bool rat_umts = true;
    bool rat_nr = true;
    bool reboot = false;
    // Cell lock
    std::string lock_earfcn_pci;// "freq:id"
    std::string lock_version;   // v1/v2/v3
    std::string lock_mode;      // 2g/3g/4g
    bool unlock_cell = false;

    // ── SIM-less / multi-PLMN scan support ──────────────────────────────
    // Force modem to data-centric mode so it doesn't refuse PLMN
    // registration when no SIM is inserted.
    bool force_data_centric = false;
    bool restore_voice_centric = false;
    bool read_ue_setting = false;
    bool simless_prep = false;
    bool read_mode_pref = false;
    uint32_t set_mode_pref_mask = 0;
    bool set_mode_pref_flag = false;

    // EFS2 browse
    std::string efs_cmd;
    std::string efs_path;
    bool efs_debug = false;
    bool asn_selftest = false;

    // LTE band preference via direct EFS NV write (bypasses broken AT$QCBANDPREF)
    bool set_lte_bands_flag = false;
    uint64_t set_lte_bands_mask = 0;
    bool read_lte_bands = false;

    // GSM ARFCN-list lock via direct EFS NV writes (Qualcomm recipe).
    bool lock_gsm_flag = false;
    std::vector<uint16_t> lock_gsm_arfcns;
    bool unlock_gsm_flag = false;
    bool read_gsm_lock = false;
};


static void print_help(const char *argv0) {
    fprintf(stderr,
            "Usage: %s [options]\n\n"
            "Options:\n"
            "  -r                Reboot modem (DIAG)\n"
            "  -t <sec>          Run for <sec> seconds then exit\n"
            "  -s <sec>          Screen refresh interval (default: 3)\n"
            "  -r <rats>         Comma-separated RAT filter: gsm,wcdma,lte,umts,nr\n"
            "  -v                Verbose: print every incoming DIAG frame\n"
            "  -n                NAS/RRC mode\n"
            "  -j <file>         Write cells to JSON file\n"
            "  --journal <file>  Write per-packet NDJSON journal\n"
            "  -l                No screen clear; plain log mode\n"
            "  -m <2g|3g|4g>     Lock mode (required with -L)\n"
            "  -L <EARFCN:PCI>   Lock LTE cell via EFS2 (e.g. -L 38100:455)\n"
            "  -lv <version>     UMTS: \n"
            "                    Version (v1(freq_lock) - /nv/item_files/wcdma/rrc/wcdma_rrc_freq_lock_item && /nv/item_files/wcdma/rrc/wcdma_rrc_enable_psc_lock)\n"
            "                    Version (v2(pci_lock) - /nv/item_files/wcdma/l1/srch/wl1_srch_debug_utils && /nv/item_files/wcdma/rrc/wcdma_rrc_freq_lock_item)\n"
            "                    LTE: \n"
            "                    Version (v1 - /nv/item_files/modem/lte/rrc/csp/pci_lock)(DEFAULT WITH -L)\n"
            "                    Version (v2 - /nv/item_files/modem/lte/rrc/csp/cell_restrict_opt_params)\n"
            "                    Version (v3 - /nv/item_files/modem/lte/rrc/efs/cell_lock_list)\n"
            "  --unlock          Remove existing cell lock and exit\n"
            "  --efs <cmd> [path] EFS2 commands: hello, ls, tree, cat, stat\n"
            "  --efs-debug       Hex dump EFS2 request/response\n"
            "  -h                This help\n",
            argv0);
}

static Options parse_args(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_help(argv[0]);
            exit(0);
        } else if (a == "-v")
            o.verbose = true;
        else if (a == "-n")
            o.nas_mode = true;
        else if (a == "-l")
            o.no_screen = true;
        else if (a == "-t" && i + 1 < argc)
            o.duration_sec = atoi(argv[++i]);
        else if (a == "-s" && i + 1 < argc)
            o.refresh_sec = atoi(argv[++i]);
        else if (a == "-j" && i + 1 < argc)
            o.json_path = argv[++i];
        else if (a == "--journal" && i + 1 < argc)
            o.journal_path = argv[++i];
        else if (a == "-r" && i + 1 < argc) {
            std::string rats = argv[++i];
            o.rat_gsm = o.rat_wcdma = o.rat_lte = o.rat_umts = o.rat_nr = false;
            size_t pos = 0;
            while (pos < rats.size()) {
                size_t end = rats.find(',', pos);
                if (end == std::string::npos) end = rats.size();
                std::string tok = rats.substr(pos, end - pos);
                if (tok == "gsm") o.rat_gsm = true;
                if (tok == "wcdma") o.rat_wcdma = true;
                if (tok == "lte") o.rat_lte = true;
                if (tok == "umts") o.rat_umts = true;
                if (tok == "nr") o.rat_nr = true;
                pos = end + 1;
            }
        } else if (a == "-m" && i + 1 < argc)
            o.lock_mode = argv[++i];
        else if (a == "-L" && i + 1 < argc)
            o.lock_earfcn_pci = argv[++i];
        else if (a == "-lv" && i + 1 < argc)
            o.lock_version = argv[++i];
        else if (a == "--unlock")
            o.unlock_cell = true;
        else if (a == "--data-centric")
            o.force_data_centric = true;
        else if (a == "--voice-centric")
            o.restore_voice_centric = true;
        else if (a == "--read-ue-setting")
            o.read_ue_setting = true;
        else if (a == "--simless-prep")
            o.simless_prep = true;
        else if (a == "--read-mode-pref")
            o.read_mode_pref = true;
        else if (a == "--set-mode-pref" && i + 1 < argc) {
            // Hex string like "0x9C010000" or decimal "131"
            const char *v = argv[++i];
            o.set_mode_pref_mask = static_cast<uint32_t>(strtoul(v, nullptr, 0));
            o.set_mode_pref_flag = true;
        } else if (a == "--efs" && i + 1 < argc) {
            o.efs_cmd = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') o.efs_path = argv[++i];
        } else if (a == "--efs-debug")
            o.efs_debug = true;
        else if (a == "--log-file" && i + 1 < argc)
            o.log_file = argv[++i];
        else if (a == "-r")
            o.reboot = true;
        else if (a == "--asn1-selftest")
            o.asn_selftest = true;
        else if (a == "--set-lte-bands" && i + 1 < argc) {
            // Hex bitmask like "0x40" (B7 only) or "0xFFFFFFFFFFFFFFFF" (all).
            // Bit N = LTE band (N+1).
            // Strip '_' separators (`0x000001E20809_18DF`) — strtoull stops
            // at the underscore otherwise, silently truncating the mask!
            std::string v = argv[++i];
            v.erase(std::remove(v.begin(), v.end(), '_'), v.end());
            o.set_lte_bands_mask = strtoull(v.c_str(), nullptr, 0);
            o.set_lte_bands_flag = true;
            fprintf(stderr, "[parse] --set-lte-bands sanitized='%s' -> 0x%016llx\n",
                    v.c_str(), (unsigned long long) o.set_lte_bands_mask);
        } else if (a == "--read-lte-bands") {
            // Print current LTE band preference from EFS and decode bands.
            o.read_lte_bands = true;
        } else if (a == "--lock-gsm-arfcns" && i + 1 < argc) {
            // CSV of GSM ARFCNs, e.g. "100,101,102" or just "100".
            std::string v = argv[++i];
            std::stringstream ss(v);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (tok.empty()) continue;
                unsigned long val = strtoul(tok.c_str(), nullptr, 0);
                if (val <= 0xFFFF) o.lock_gsm_arfcns.push_back(static_cast<uint16_t>(val));
            }
            o.lock_gsm_flag = !o.lock_gsm_arfcns.empty();
            if (!o.lock_gsm_flag)
                fprintf(stderr, "[parse] --lock-gsm-arfcns: no valid ARFCNs parsed from '%s'\n", v.c_str());
        } else if (a == "--unlock-gsm") {
            o.unlock_gsm_flag = true;
        } else if (a == "--read-gsm-lock") {
            o.read_gsm_lock = true;
        } else {
            // Tolerant: warn and skip unknown options instead of aborting. A
            // version mismatch between the Kotlin caller (which may append newer
            // flags like --log-file) and an older binary must NOT brick a lock
            // or reboot. The old behaviour (print_help + exit(1)) turned an
            // unknown flag into "Fail: Exit1" mid-reboot.
            fprintf(stderr, "[warn] ignoring unknown option: %s\n", a.c_str());
        }
    }
    return o;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell database
// ─────────────────────────────────────────────────────────────────────────────
struct CellRecord {
    enum class Type { GSM,
                      WCDMA,
                      LTE,
                      UMTS,
                      NR } type;

    uint32_t freq = 0;    // ARFCN/UARFCN/EARFCN/NRARFCN
    uint32_t id = 0;      // PCI / PSC / BSIC
    int16_t rsrp_rscp = 0;// dBm
    int16_t rsrq_ecno = 0;// dB
    int16_t rssi = 0;
    uint8_t dl_bw = 0;// DL bandwidth (raw B0C2 field; 0 = unknown)
    uint8_t ul_bw = 0;// UL bandwidth (raw B0C2 field; 0 = unknown)
    bool serving = false;

    // Identity fields
    int32_t cell_id = -1;// E-UTRAN CID / UTRAN CID / GSM CID
    uint16_t tac_lac = 0;// TAC (LTE/NR) or LAC (GSM/WCDMA)
    uint32_t mcc = 0;
    uint32_t mnc = 0;

    // GSM-only extras
    uint8_t ncc = 0xFF;    // 0xFF = unknown
    uint8_t bcc = 0xFF;    // 0xFF = unknown
    int16_t c1 = INT16_MIN;// INT16_MIN = unknown
    int16_t c2 = INT16_MIN;// INT16_MIN = unknown

    // Metadata
    time_t first_seen = 0;
    time_t last_seen = 0;
    uint32_t seen_count = 0;

    std::string signal_bar() const {
        int v = rsrp_rscp;
        int bars;
        if (type == Type::LTE || type == Type::NR) {
            if (v >= -80) bars = 5;
            else if (v >= -95)
                bars = 4;
            else if (v >= -105)
                bars = 3;
            else if (v >= -115)
                bars = 2;
            else
                bars = 1;
        } else if (type == Type::WCDMA || type == Type::UMTS) {
            if (v >= -75) bars = 5;
            else if (v >= -85)
                bars = 4;
            else if (v >= -95)
                bars = 3;
            else if (v >= -105)
                bars = 2;
            else
                bars = 1;
        } else {
            if (v >= -70) bars = 5;
            else if (v >= -80)
                bars = 4;
            else if (v >= -90)
                bars = 3;
            else if (v >= -100)
                bars = 2;
            else
                bars = 1;
        }
        std::string s;
        for (int i = 0; i < 5; ++i) s += (i < bars) ? "█" : "░";
        return s;
    }

    // Format MCCMNC string e.g. "25011" or "---"
    std::string mccmnc_str() const {
        if (mcc == 0) return "---";
        char buf[16];
        if (mnc < 10) snprintf(buf, sizeof(buf), "%u%02u", mcc, mnc);
        else
            snprintf(buf, sizeof(buf), "%u%u", mcc, mnc);
        return buf;
    }

    // Format CID string e.g. "19981501" or "-"
    std::string cid_str() const {
        if (cell_id < 0) return "-";
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", cell_id);
        return buf;
    }

    // Format TAC/LAC string
    std::string taclac_str() const {
        if (tac_lac == 0) return "-";
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", tac_lac);
        return buf;
    }
};

struct CellDb {
    std::mutex mx;
    std::unordered_map<uint64_t, CellRecord> cells;
    uint64_t update_seq = 0;

    uint64_t key(CellRecord::Type t, uint32_t freq, uint32_t id) const {
        return ((uint64_t) t << 48) | ((uint64_t) (freq & 0xFFFFF) << 16) | (id & 0xFFFF);
    }

    void upsert(CellRecord rec) {
        uint64_t k;
        if (rec.type == CellRecord::Type::GSM && rec.cell_id > 0) {
            // CID is the AUTHORITATIVE GSM cell identity. Key by CID regardless
            // of ARFCN so: (a) a mis-decoded BSIC can't split one physical cell
            // into duplicates (same CID + different BSIC is still ONE cell), and
            // (b) two distinct cells reusing a BCCH ARFCN (different CID) stay
            // separate. Bit 47 separates CID-keyed from ARFCN-keyed entries.
            k = ((uint64_t) rec.type << 48) | (1ULL << 47) |
                ((uint32_t) rec.cell_id & 0xFFFF);
        } else if (rec.type == CellRecord::Type::GSM && rec.freq != 0) {
            // No CID yet (measurement-only neighbour): key by ARFCN so the L1
            // RxLev read and a later same-ARFCN identity read line up. BSIC is
            // deliberately NOT in the key — it is noisy and would split a cell.
            k = ((uint64_t) rec.type << 48) | ((uint64_t) (rec.freq & 0xFFFFF) << 16);
        } else {
            k = key(rec.type, rec.freq, rec.id);
        }
        std::lock_guard<std::mutex> lk(mx);
        auto it = cells.find(k);
        if (it == cells.end()) {
            rec.first_seen = rec.last_seen = time(nullptr);
            rec.seen_count = 1;
            cells[k] = rec;
        } else {
            CellRecord &e = it->second;
            // Only overwrite signal when the new record actually carries a
            // measurement. RRC-identity updates (CID/TAC, signal=0) must NOT
            // wipe the ML1 RSRP/RSRQ/RSSI captured for the same cell.
            if (rec.rsrp_rscp != 0) e.rsrp_rscp = rec.rsrp_rscp;
            if (rec.rsrq_ecno != 0) e.rsrq_ecno = rec.rsrq_ecno;
            if (rec.rssi != 0) e.rssi = rec.rssi;
            if (rec.dl_bw) e.dl_bw = rec.dl_bw;
            if (rec.ul_bw) e.ul_bw = rec.ul_bw;
            e.serving = rec.serving;
            // Identity-overwrite guard: if this row already has an established
            // PLMN+CID and the incoming record carries a DIFFERENT non-zero CID,
            // it's a different physical cell that merely shares the key — keep
            // the established identity (don't clobber PLMN/CID/LAC).
            const bool identity_locked =
                    (e.mcc > 0 && e.cell_id > 0 && rec.cell_id > 0 && rec.cell_id != e.cell_id);
            if (!identity_locked) {
                if (rec.cell_id >= 0) e.cell_id = rec.cell_id;
                if (rec.tac_lac > 0) e.tac_lac = rec.tac_lac;
                if (rec.mcc > 0) {
                    e.mcc = rec.mcc;
                    e.mnc = rec.mnc;
                }
            }
            if (rec.type == CellRecord::Type::GSM && rec.id != 0xFF) e.id = rec.id;// BSIC from L1 meas; SI-3 identity reads carry 0xFF
            if (rec.ncc != 0xFF) e.ncc = rec.ncc;
            if (rec.bcc != 0xFF) e.bcc = rec.bcc;
            if (rec.c1 != INT16_MIN) e.c1 = rec.c1;
            if (rec.c2 != INT16_MIN) e.c2 = rec.c2;
            e.last_seen = time(nullptr);
            e.seen_count++;
        }
        update_seq++;
    }

    std::vector<CellRecord> snapshot() {
        std::lock_guard<std::mutex> lk(mx);
        std::vector<CellRecord> v;
        v.reserve(cells.size());
        for (auto &kv: cells) v.push_back(kv.second);
        std::sort(v.begin(), v.end(), [](const CellRecord &a, const CellRecord &b) {
            if (a.serving != b.serving) return a.serving > b.serving;
            if (a.type != b.type) return a.type < b.type;
            return a.rsrp_rscp > b.rsrp_rscp;
        });
        return v;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// NAS log ring buffer
// ─────────────────────────────────────────────────────────────────────────────
struct NasEntry {
    time_t ts;
    bool uplink;
    uint8_t pd, msg_type;
    std::string name;
    size_t pdu_len;
};
struct NasLog {
    std::mutex mx;
    std::vector<NasEntry> entries;
    static constexpr size_t MAX = 40;
    void push(NasEntry e) {
        std::lock_guard<std::mutex> lk(mx);
        entries.push_back(std::move(e));
        if (entries.size() > MAX) entries.erase(entries.begin());
    }
    std::vector<NasEntry> snapshot() {
        std::lock_guard<std::mutex> lk(mx);
        return entries;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Stats
// ─────────────────────────────────────────────────────────────────────────────
struct AppStats {
    std::atomic<uint64_t> frames_total{0};
    std::atomic<uint64_t> frames_gsm{0}, frames_wcdma{0}, frames_lte{0};
    std::atomic<uint64_t> frames_umts{0}, frames_nr{0};
    std::atomic<uint64_t> parse_errors{0}, nas_messages{0}, rrc_messages{0};
    time_t start_time{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Formatting helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::string fmt_time(time_t t) {
    char buf[32];
    struct tm ti;
    localtime_r(&t, &ti);
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return buf;
}
static std::string fmt_duration(time_t s) {
    char buf[32];
    if (s < 60) snprintf(buf, sizeof(buf), "%llds", (long long) s);
    else if (s < 3600)
        snprintf(buf, sizeof(buf), "%lldm%02llds", (long long) s / 60, (long long) s % 60);
    else
        snprintf(buf, sizeof(buf), "%lldh%02lldm", (long long) s / 3600, (long long) (s % 3600) / 60);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON writer (with all identity fields)
// ─────────────────────────────────────────────────────────────────────────────
static void write_json(const std::string &path, const std::vector<CellRecord> &cells,
                       const AppStats &stats) {
    FILE *f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "[WARN] Cannot write JSON: %s\n", strerror(errno));
        return;
    }

    time_t now = time(nullptr);
    char ts[64];
    struct tm ti;
    gmtime_r(&now, &ti);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &ti);

    fprintf(f, "{\n  \"timestamp\": \"%s\",\n", ts);
    fprintf(f, "  \"duration_sec\": %lld,\n", (long long) (now - stats.start_time));
    fprintf(f, "  \"frames_total\": %llu,\n", (unsigned long long) stats.frames_total.load());
    fprintf(f, "  \"cells\": [\n");

    for (size_t i = 0; i < cells.size(); ++i) {
        const auto &c = cells[i];
        const char *rat_str;
        switch (c.type) {
            case CellRecord::Type::GSM:
                rat_str = "GSM";
                break;
            case CellRecord::Type::WCDMA:
                rat_str = "WCDMA";
                break;
            case CellRecord::Type::LTE:
                rat_str = "LTE";
                break;
            case CellRecord::Type::UMTS:
                rat_str = "UMTS";
                break;
            case CellRecord::Type::NR:
                rat_str = "NR";
                break;
        }
        fprintf(f,
                "    {\n"
                "      \"rat\": \"%s\",\n"
                "      \"arfcn\": %u,\n"
                "      \"band\": \"%s\",\n"
                "      \"pci\": %u,\n"
                "      \"ncc\": %d,\n"
                "      \"bcc\": %d,\n"
                "      \"rsrp\": %d,\n"
                "      \"rsrq\": %d,\n"
                "      \"rssi\": %d,\n"
                "      \"dl_bw\": %u,\n"
                "      \"ul_bw\": %u,\n"
                "      \"c1\": %s,\n"
                "      \"c2\": %s,\n"
                "      \"serving\": %s,\n"
                "      \"cell_id\": %d,\n"
                "      \"tac\": %u,\n"
                "      \"mcc\": %u,\n"
                "      \"mnc\": %u,\n"
                "      \"mccmnc\": \"%s\",\n"
                "      \"seen_count\": %u,\n"
                "      \"first_seen\": \"%s\",\n"
                "      \"last_seen\": \"%s\"\n"
                "    }%s\n",
                rat_str, c.freq,
                (c.type == CellRecord::Type::GSM)
                        ? gsm_metrics::gsm_band_name(static_cast<uint16_t>(c.freq))
                        : "",
                c.id,
                (c.ncc != 0xFF) ? (int) c.ncc : -1,
                (c.bcc != 0xFF) ? (int) c.bcc : -1,
                c.rsrp_rscp, c.rsrq_ecno, c.rssi,
                c.dl_bw, c.ul_bw,
                (c.c1 != INT16_MIN) ? std::to_string(c.c1).c_str() : "null",
                (c.c2 != INT16_MIN) ? std::to_string(c.c2).c_str() : "null",
                c.serving ? "true" : "false",
                c.cell_id, c.tac_lac,
                c.mcc, c.mnc, c.mccmnc_str().c_str(),
                c.seen_count,
                fmt_time(c.first_seen).c_str(),
                fmt_time(c.last_seen).c_str(),
                (i + 1 < cells.size()) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Console rendering
// ─────────────────────────────────────────────────────────────────────────────
static const char *rat_color(CellRecord::Type t) {
    switch (t) {
        case CellRecord::Type::GSM:
            return MAGENTA;
        case CellRecord::Type::WCDMA:
            return YELLOW;
        case CellRecord::Type::LTE:
            return GREEN;
        case CellRecord::Type::UMTS:
            return CYAN;
        case CellRecord::Type::NR:
            return BLUE;
    }
    return WHITE;
}
static const char *rat_name(CellRecord::Type t) {
    switch (t) {
        case CellRecord::Type::GSM:
            return "GSM  ";
        case CellRecord::Type::WCDMA:
            return "WCDMA";
        case CellRecord::Type::LTE:
            return "LTE  ";
        case CellRecord::Type::UMTS:
            return "UMTS ";
        case CellRecord::Type::NR:
            return "NR   ";
    }
    return "?????";
}
static const char *rsrp_color(int v) {
    if (v >= -85) return GREEN;
    if (v >= -100) return YELLOW;
    return RED;
}

static void render_screen(const std::vector<CellRecord> &cells,
                          const AppStats &stats, const Options &opts,
                          const std::string &status_line) {
    time_t now = time(nullptr);
    time_t uptime = now - stats.start_time;

    if (!opts.no_screen) printf("%s", CLS);

    printf("%s%s╔══════════════════════════════════════════════════════════════════════════════════════╗%s\n", BOLD, CYAN, RESET);
    printf("%s%s║   Qualcomm DIAG Scanner  ·  uptime %-8s  ·  %s                                ║%s\n",
           BOLD, CYAN, fmt_duration(uptime).c_str(), fmt_time(now).c_str(), RESET);
    printf("%s%s╚══════════════════════════════════════════════════════════════════════════════════════╝%s\n", BOLD, CYAN, RESET);

    printf(" %sFrames%s: %llu  "
           "%sGSM%s:%llu  %sWCDMA%s:%llu  %sLTE%s:%llu  %sUMTS%s:%llu  %sNR%s:%llu  "
           "%sErr%s:%llu  %sNAS%s:%llu\n",
           BOLD, RESET, (unsigned long long) stats.frames_total.load(),
           MAGENTA, RESET, (unsigned long long) stats.frames_gsm.load(),
           YELLOW, RESET, (unsigned long long) stats.frames_wcdma.load(),
           GREEN, RESET, (unsigned long long) stats.frames_lte.load(),
           CYAN, RESET, (unsigned long long) stats.frames_umts.load(),
           BLUE, RESET, (unsigned long long) stats.frames_nr.load(),
           RED, RESET, (unsigned long long) stats.parse_errors.load(),
           WHITE, RESET, (unsigned long long) stats.nas_messages.load());

    if (!status_line.empty())
        printf(" %s%s%s\n", DIM, status_line.c_str(), RESET);

    if (cells.empty()) {
        printf("\n %sWaiting for cells...%s\n", DIM, RESET);
    } else {
        // ── Table header ────────────────────────────────────────────────
        // Header labels are RAT-agnostic since the table mixes GSM/WCDMA/LTE/NR:
        //   ID    = PCI (LTE/NR) / PSC (WCDMA) / BSIC (GSM)
        //   Sig   = RSRP (LTE/NR) / RSCP (WCDMA) / RxLev (GSM), all in dBm
        //   Qual  = RSRQ (LTE/NR) / Ec/No (WCDMA) / —     (GSM), all in dB
        //   TAC   = TAC (LTE/NR)  / LAC  (WCDMA/GSM)
        printf("\n%s %-5s  %-7s %-5s  %-5s %-5s  %-6s  %-10s  %-6s  Signal  %s%s\n",
               BOLD,
               "RAT", "ARFCN", "ID", "Sig", "Qual", "MCCMNC", "CID", "TAC", "Seen",
               RESET);
        printf("%s %s%s\n", DIM, std::string(88, '-').c_str(), RESET);

        for (const auto &c: cells) {
            const char *mark = c.serving ? "◄" : " ";

            printf(" %s%s%s%s %-7u %-5u  %s%4d%s %4d  %-6s  %-10s  %-6s  %s  %s%4u %s%s\n",
                   rat_color(c.type), rat_name(c.type), RESET,
                   mark,
                   c.freq, c.id,
                   rsrp_color(c.rsrp_rscp), c.rsrp_rscp, RESET,
                   c.rsrq_ecno,
                   c.mccmnc_str().c_str(),
                   c.cid_str().c_str(),
                   c.taclac_str().c_str(),
                   c.signal_bar().c_str(),
                   DIM, c.seen_count,
                   fmt_time(c.last_seen).c_str(), RESET);
        }
    }

    printf("\n%s Press Ctrl-C to stop%s\n", DIM, RESET);
    fflush(stdout);
}

static void render_nas_screen(const std::vector<NasEntry> &nas_log,
                              const AppStats &stats, const Options &opts) {
    time_t now = time(nullptr);
    if (!opts.no_screen) printf("%s", CLS);
    printf("%s%s║ NAS/RRC Monitor · uptime %-8s ║%s\n",
           BOLD, CYAN, fmt_duration(now - stats.start_time).c_str(), RESET);
    printf(" NAS: %llu  RRC: %llu\n\n",
           (unsigned long long) stats.nas_messages.load(),
           (unsigned long long) stats.rrc_messages.load());
    if (nas_log.empty()) {
        printf(" %sNo NAS messages yet...%s\n", DIM, RESET);
    } else {
        printf("%s %-8s %-4s %-4s %-30s Size%s\n", BOLD, "Time", "Dir", "MT", "Message", RESET);
        for (const auto &e: nas_log) {
            printf(" %-8s %s%-4s%s %02X  %-30s %zu\n",
                   fmt_time(e.ts).c_str(),
                   e.uplink ? GREEN : BLUE, e.uplink ? "UL" : "DL", RESET,
                   e.msg_type, e.name.c_str(), e.pdu_len);
        }
    }
    printf("\n%s Press Ctrl-C to stop%s\n", DIM, RESET);
    fflush(stdout);
}

static FILE *g_journal = nullptr;
static std::unordered_map<uint16_t, uint64_t> g_code_hist;// every received log code → count
static std::mutex g_code_hist_mx;

static const char *rat_name(Rat r) {
    switch (r) {
        case Rat::GSM:
            return "GSM";
        case Rat::WCDMA:
            return "WCDMA";
        case Rat::UMTS:
            return "UMTS";
        case Rat::NR:
            return "NR";
        case Rat::LTE_RRC:
            return "LTE_RRC";
        case Rat::LTE_NAS:
            return "LTE_NAS";
        case Rat::LTE:
            return "LTE";
        default:
            return "UNKNOWN";
    }
}

// JSON-escape a string for NDJSON output.
static std::string json_esc(const std::string &in) {
    std::string o;
    o.reserve(in.size() + 8);
    for (char c: in) {
        switch (c) {
            case '"':
                o += "\\\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\r':
                o += "\\r";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) o += c;
        }
    }
    return o;
}

// Sink: serialise one JournalRecord as an NDJSON line to the --journal file.
static void journal_sink_to_file(const JournalRecord &r) {
    if (!g_journal) return;
    fprintf(g_journal,
            "{\"t\":%.3f,\"code\":%u,\"hex\":\"0x%04X\",\"rat\":\"%s\","
            "\"chan\":\"%s\",\"msg\":\"%s\",\"sum\":\"%s\","
            "\"raw\":\"%s\",\"detail\":\"%s\",\"len\":%zu}\n",
            r.t, r.code, r.code, r.rat,
            json_esc(r.channel).c_str(), json_esc(r.msg_type).c_str(),
            json_esc(r.summary).c_str(),
            json_esc(r.raw).c_str(), json_esc(r.detail).c_str(), r.len);
    fflush(g_journal);
}

static void log_frame(const DiagFrame &f) {
    const char *col = WHITE;
    switch (f.rat) {
        case Rat::GSM:
            col = MAGENTA;
            break;
        case Rat::WCDMA:
            col = YELLOW;
            break;
        case Rat::LTE:
        case Rat::LTE_NAS:
            col = GREEN;
            break;
        case Rat::UMTS:
            col = CYAN;
            break;
        case Rat::NR:
            col = BLUE;
            break;
        default:
            break;
    }
    printf("%s[%s] code=0x%04X len=%-4zu  %s%s\n",
           col, fmt_time(time(nullptr)).c_str(),
           f.log_code, f.payload_len,
           QualcommLogParser::log_code_name(f.log_code), RESET);
    fflush(stdout);
}


static bool parse_lock_target(const std::string &s, uint32_t &left, uint16_t &right) {
    auto colon = s.find(':');
    if (colon == std::string::npos) return false;
    left = static_cast<uint32_t>(atoi(s.substr(0, colon).c_str()));
    right = static_cast<uint16_t>(atoi(s.c_str() + colon + 1));
    return true;
}

// Parse CSV of "freq:id" pairs: "38100:455,38100:289,1721:34"
// A single pair (no commas) is also valid and produces a vector of size 1.
// Returns false if any token is malformed or the input contains no valid pairs.
static bool parse_lock_targets(const std::string &s,
                               std::vector<std::pair<uint32_t, uint16_t>> &out) {
    out.clear();
    size_t pos = 0;
    while (pos < s.size()) {
        size_t comma = s.find(',', pos);
        std::string tok = s.substr(pos, (comma == std::string::npos ? s.size() : comma) - pos);
        pos = (comma == std::string::npos) ? s.size() : comma + 1;
        if (tok.empty()) continue;

        uint32_t left = 0;
        uint16_t right = 0;
        if (!parse_lock_target(tok, left, right)) {
            fprintf(stderr, "ERROR: -L: bad pair '%s' (expected freq:id)\n", tok.c_str());
            return false;
        }
        out.push_back({left, right});
    }
    return !out.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// main()
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
    // Active-scan helper: `diag_scan setmode <gsm|wcdma|lte|auto>`. Writes CM
    // system-selection preference to force a single-RAT acquisition sweep across
    // the whole band (every detectable cell then shows up in the logs). ALWAYS
    // run `setmode auto` afterwards or the modem stays single-RAT (no data/LTE).
    if (argc >= 2 && std::string(argv[1]) == "setmode") {
        std::string m = (argc >= 3) ? argv[2] : "auto";
        std::string plmn = (argc >= 4) ? argv[3] : "";// optional MCC-MNC for CM PLMN
        uint32_t mode = 2;                            // AUTOMATIC (restore)
        if (m == "gsm") mode = 13;                    // GSM_ONLY
        else if (m == "wcdma")
            mode = 14;// WCDMA_ONLY
        else if (m == "lte")
            mode = 38;// LTE_ONLY
        else if (m == "auto")
            mode = 2;// AUTOMATIC
        else {
            fprintf(stderr, "usage: %s setmode <gsm|wcdma|lte|auto> [mccmnc]\n", argv[0]);
            return 2;
        }
        DiagDciClient sm_client;
        if (!sm_client.start()) {
            fprintf(stderr, "[SETMODE] DCI start failed\n");
            return 1;
        }
        sm_client.send_syssel_pref(mode, plmn);
        std::this_thread::sleep_for(std::chrono::seconds(2));// let the modem apply
        sm_client.stop();
        fprintf(stderr, "[SETMODE] %s plmn=%s (mode_pref=%u) done\n", m.c_str(), plmn.empty() ? "99999" : plmn.c_str(), mode);
        return 0;
    }

    Options opts = parse_args(argc, argv);

    // --log-file: tee EVERY stderr diagnostic (EFS/lock/reboot/parser) into a
    // file while still echoing to the original stderr (which the Kotlin side
    // captures). Uses toybox `tee`: our stderr → tee stdin → file + original
    // stderr (tee's stdout redirected to fd2 via '1>&2'). Zero changes to the
    // hundreds of existing fprintf(stderr,...) call sites.
    if (!opts.log_file.empty()) {
        std::string esc;// single-quote escape for shell
        for (char c: opts.log_file) {
            if (c == '\'') esc += "'\\''";
            else
                esc += c;
        }
        std::string cmd = "tee -a '" + esc + "' 1>&2";
        FILE *tee = popen(cmd.c_str(), "w");
        if (tee) {
            fflush(stderr);
            dup2(fileno(tee), 2);
            setvbuf(stderr, nullptr, _IOLBF, 0);
            fprintf(stderr, "[LOG] === diag_scan log start (pid=%d) ===\n", (int) getpid());
        }
    }

    g_color = (isatty(fileno(stdout)) != 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    CellDb db;
    NasLog nas_log;
    AppStats stats;
    stats.start_time = time(nullptr);
    std::string last_status;
    std::mutex status_mx;

    DiagDciClient client;

    bool reboot_only = opts.reboot;
    (void) reboot_only;// reserved for future use — silences -Wunused-variable
    bool lock_only = !opts.lock_earfcn_pci.empty() || opts.unlock_cell ||
                     opts.force_data_centric || opts.restore_voice_centric ||
                     opts.read_ue_setting ||
                     opts.simless_prep || opts.read_mode_pref || opts.set_mode_pref_flag ||
                     opts.set_lte_bands_flag || opts.read_lte_bands ||
                     !opts.efs_cmd.empty();
    // ── RAT filter ──────────────────────────────────────────────────────────
    if (!lock_only) {
        if (!(opts.rat_gsm && opts.rat_wcdma && opts.rat_lte && opts.rat_umts && opts.rat_nr)) {
            std::vector<uint16_t> filtered;
            auto all = QualcommLogParser::all_log_codes();
            for (uint16_t c: all) {
                Rat r = QualcommLogParser::log_code_rat(c);
                if (r == Rat::GSM && opts.rat_gsm) {
                    filtered.push_back(c);
                    continue;
                }
                if (r == Rat::WCDMA && opts.rat_wcdma) {
                    filtered.push_back(c);
                    continue;
                }
                if ((r == Rat::LTE || r == Rat::LTE_NAS) && opts.rat_lte) {
                    filtered.push_back(c);
                    continue;
                }
                if (r == Rat::UMTS && opts.rat_umts) {
                    filtered.push_back(c);
                    continue;
                }
                if (r == Rat::NR && opts.rat_nr) {
                    filtered.push_back(c);
                    continue;
                }
            }
            client.set_log_codes(filtered);
        }

        client.set_neighbor_callback([&](const ParsedNeighbors &n) {
            for (const auto &c: n.lte) {
                if (!opts.rat_lte) continue;
                CellRecord r{};
                r.type = CellRecord::Type::LTE;
                r.freq = c.earfcn;
                r.id = c.pci;
                r.rsrp_rscp = c.rsrp;
                r.rsrq_ecno = c.rsrq;
                r.rssi = c.rssi;
                r.dl_bw = c.dl_bw;
                r.ul_bw = c.ul_bw;
                r.serving = c.serving;
                r.cell_id = c.cell_id;
                r.tac_lac = c.tac;
                r.mcc = c.mcc;
                r.mnc = c.mnc;
                db.upsert(r);
            }
            for (const auto &c: n.wcdma) {
                if (!opts.rat_wcdma) continue;
                CellRecord r{};
                r.type = CellRecord::Type::WCDMA;
                r.freq = c.uarfcn;
                r.id = c.psc;
                r.rsrp_rscp = c.rscp;
                r.rsrq_ecno = c.ecno;
                r.serving = c.serving;
                r.cell_id = static_cast<int32_t>(c.cid);
                r.tac_lac = c.lac;
                r.mcc = c.mcc;
                r.mnc = c.mnc;
                db.upsert(r);
            }
            for (const auto &c: n.gsm) {
                if (!opts.rat_gsm) continue;
                CellRecord r{};
                r.type = CellRecord::Type::GSM;
                r.freq = c.arfcn;
                r.id = c.bsic;// BSIC (0-63); 0xFF (255) = unknown (no SCH sync)
                r.rsrp_rscp = c.rxlev;
                r.serving = c.serving;
                r.cell_id = c.cid ? static_cast<int32_t>(c.cid) : -1;// 0 = unknown (don't clobber merged identity CID)
                r.tac_lac = c.lac;
                r.mcc = c.mcc;
                r.mnc = c.mnc;
                r.ncc = c.ncc;
                r.bcc = c.bcc;
                r.c1 = c.c1;
                r.c2 = c.c2;
                db.upsert(r);
            }
            for (const auto &c: n.umts) {
                if (!opts.rat_umts) continue;
                CellRecord r{};
                r.type = CellRecord::Type::UMTS;
                r.freq = c.uarfcn;
                r.id = c.psc;
                r.rsrp_rscp = c.rscp;
                r.rsrq_ecno = c.ecno;
                r.serving = c.serving;
                r.cell_id = static_cast<int32_t>(c.cid);
                r.tac_lac = c.lac;
                r.mcc = c.mcc;
                r.mnc = c.mnc;
                db.upsert(r);
            }
            for (const auto &c: n.nr) {
                if (!opts.rat_nr) continue;
                CellRecord r{};
                r.type = CellRecord::Type::NR;
                r.freq = c.nrarfcn;
                r.id = c.pci;
                r.rsrp_rscp = c.ss_rsrp;
                r.rsrq_ecno = c.ss_rsrq;
                r.serving = c.serving;
                r.cell_id = c.cell_id;
                r.tac_lac = static_cast<uint16_t>(c.tac);
                r.mcc = c.mcc;
                r.mnc = c.mnc;
                db.upsert(r);
            }
        });

        if (!opts.journal_path.empty()) {
            g_journal = fopen(opts.journal_path.c_str(), "w");
            if (!g_journal) fprintf(stderr, "[warn] cannot open journal %s\n", opts.journal_path.c_str());
            else
                journal_set_sink(journal_sink_to_file);
        }
        client.set_raw_frame_callback([&](const DiagFrame &f) {
            stats.frames_total++;
            switch (f.rat) {
                case Rat::GSM:
                    stats.frames_gsm++;
                    break;
                case Rat::WCDMA:
                    stats.frames_wcdma++;
                    break;
                case Rat::LTE:
                case Rat::LTE_NAS:
                    stats.frames_lte++;
                    break;
                case Rat::UMTS:
                    stats.frames_umts++;
                    break;
                case Rat::NR:
                    stats.frames_nr++;
                    break;
                default:
                    break;
            }
            if (opts.verbose && opts.no_screen) log_frame(f);
            {
                std::lock_guard<std::mutex> _hl(g_code_hist_mx);
                g_code_hist[f.log_code]++;
            }
            // Generic journal entry per frame, EXCEPT codes a dedicated parser emits
            // decoded: 0xB0C0 LTE RRC, 0x412F WCDMA RRC, 0xB17F/B180/B193/B197 LTE ML1,
            // 0x512F/0x5B2F GSM RR (System Information).
            if (g_journal && f.log_code != 0xB0C0 && f.log_code != 0x412F &&
                f.log_code != 0xB17F && f.log_code != 0xB180 &&
                f.log_code != 0xB193 && f.log_code != 0xB197 &&
                f.log_code != 0x512F && f.log_code != 0x5B2F) {
                JournalRecord jr;
                jr.t = f.timestamp_unix;
                jr.code = f.log_code;
                jr.rat = rat_name(f.rat);
                jr.len = f.payload_len;
                jr.msg_type = QualcommLogParser::log_code_name(f.log_code);
                jr.raw = journal_hex(f.payload, f.payload_len);
                journal_emit(jr);
            }
        });

        client.set_nas_callback([&](const UmtsNasMessage &m) {
            stats.nas_messages++;
            NasEntry e{};
            e.ts = time(nullptr);
            e.uplink = m.is_uplink;
            e.pd = m.pd;
            e.msg_type = m.msg_type;
            e.name = DiagUmtsLogParser::nas_msg_name(m.pd, m.msg_type);
            e.pdu_len = m.pdu.size();
            nas_log.push(e);
        });

        client.set_rrc_callback([&](const UmtsRrcMessage &m) {
            stats.rrc_messages++;
            if (opts.nas_mode || opts.verbose) {
                printf("[%s] %sRRC%s %s RB=%u len=%zu\n",
                       fmt_time(time(nullptr)).c_str(),
                       CYAN, RESET,
                       DiagUmtsLogParser::rrc_channel_name(m.channel),
                       m.rb_id, m.pdu.size());
                fflush(stdout);
            }
        });
    }

    printf("%s%sQualcomm DIAG Scanner — starting...%s\n", BOLD, CYAN, RESET);
    fflush(stdout);

    if (!client.start()) {
        fprintf(stderr,
                "\n%s[ERROR]%s Could not start DIAG DCI client.\n"
                "  Possible: libdiag.so not found, no permissions, or no modem\n",
                RED, RESET);
        return 1;
    }

    printf("%s[OK]%s DIAG DCI started (client_id=%d, %zu log codes)\n",
           GREEN, RESET, client.client_id(),
           QualcommLogParser::all_log_codes().size());

    DiagCellLock locker(client.client_id());
    if (opts.efs_debug || opts.verbose) {
        locker.set_debug(true);
    }

    // Register locker for emergency signal cleanup. If user Ctrl-Cs after a
    // lock is applied, our signal handler will unlink the lock files instead
    // of leaving the modem stuck.
    g_locker_for_cleanup = &locker;

    if (opts.unlock_cell) {
        printf("Removing ALL cell locks (LTE + UMTS/WCDMA + GSM) via EFS2...\n");
        int r = locker.unlock_all();
        printf("  unlock_all: %s (errno=%d)\n",
               r == 0 ? "OK" : "FAILED", locker.last_errno());
        printf("Reboot modem or toggle airplane mode to apply.\n");
        client.stop();
        return 0;
    }

    // ── LTE band preference via direct EFS NV write ──────────────────
    // Bypasses the broken AT$QCBANDPREF on Xiaomi (legacy CDMA/GSM only).
    if (opts.set_lte_bands_flag) {
        printf("Setting LTE band preference: mask=0x%016llx\n",
               (unsigned long long) opts.set_lte_bands_mask);
        int r = locker.set_lte_band_pref(opts.set_lte_bands_mask);
        printf("  EFS write %s -> %s (errno=%d)\n",
               DiagCellLock::LTE_BAND_PREF_PATH,
               r == 0 ? "OK" : "FAILED", locker.last_errno());
        printf("Reboot modem or toggle airplane mode to apply.\n");
        client.stop();
        return r == 0 ? 0 : 1;
    }

    // ── Read current LTE band preference (verification helper) ───────
    if (opts.read_lte_bands) {
        uint64_t mask = locker.read_lte_band_pref();
        printf("LTE band pref @ %s\n  raw = 0x%016llx\n  bands:",
               DiagCellLock::LTE_BAND_PREF_PATH, (unsigned long long) mask);
        if (mask == 0) {
            printf(" (none — file empty or read failed; errno=%d)",
                   locker.last_errno());
        } else {
            for (int b = 1; b <= 64; ++b) {
                if (mask & (1ULL << (b - 1))) printf(" B%d", b);
            }
        }
        printf("\n");
        client.stop();
        return 0;
    }

    // ── GSM ARFCN-list lock via direct EFS NV writes (Qualcomm recipe) ──
    if (opts.lock_gsm_flag) {
        printf("Locking GSM to %zu ARFCN(s) via EFS NV writes...\n",
               opts.lock_gsm_arfcns.size());
        int r = locker.lock_gsm_arfcns(opts.lock_gsm_arfcns);
        printf("  GSM lock %s (errno=%d)\n",
               r == 0 ? "OK" : "FAILED", locker.last_errno());
        printf("Reboot modem to apply.\n");
        client.stop();
        return r == 0 ? 0 : 1;
    }

    if (opts.unlock_gsm_flag) {
        printf("Clearing GSM ARFCN lock via EFS...\n");
        int r = locker.unlock_gsm();
        printf("  GSM unlock %s (errno=%d)\n",
               r == 0 ? "OK" : "FAILED", locker.last_errno());
        printf("Reboot modem to apply.\n");
        client.stop();
        return r == 0 ? 0 : 1;
    }

    if (opts.read_gsm_lock) {
        auto list = locker.read_gsm_arfcn_list();
        bool en = locker.is_gsm_arfcn_lock_enabled();
        printf("GSM ARFCN lock state:\n  feature_flag = %s\n  ARFCNs (%zu):",
               en ? "ENABLED" : "disabled/absent", list.size());
        for (uint16_t a: list) printf(" %u", a);
        printf("\n");
        client.stop();
        return 0;
    }

    // ── --efs cat <path> : print EFS file content as hex ──────────────
    if (!opts.efs_cmd.empty()) {
        if (opts.efs_cmd == "cat" && !opts.efs_path.empty()) {
            std::vector<uint8_t> data;
            int rc = locker.read_efs_for_cli(opts.efs_path.c_str(), data);
            if (rc != 0) {
                fprintf(stderr, "[FAIL] efs read '%s' errno=%d\n",
                        opts.efs_path.c_str(), locker.last_errno());
                client.stop();
                return 1;
            }
            printf("%s (%zu bytes):\n", opts.efs_path.c_str(), data.size());
            for (size_t i = 0; i < data.size(); i += 16) {
                printf("%08zx: ", i);
                for (size_t j = 0; j < 16; ++j) {
                    if (i + j < data.size()) printf("%02x ", data[i + j]);
                    else
                        printf("   ");
                }
                printf(" ");
                for (size_t j = 0; j < 16 && (i + j) < data.size(); ++j) {
                    uint8_t c = data[i + j];
                    putchar((c >= 0x20 && c < 0x7f) ? c : '.');
                }
                putchar('\n');
            }
            client.stop();
            return 0;
        }
        fprintf(stderr, "Unknown --efs command: '%s' (supported: cat)\n",
                opts.efs_cmd.c_str());
        client.stop();
        return 1;
    }

    // ── SIM-less mode: ue_usage_setting management ──────────────────────
    if (opts.read_ue_setting) {
        int v = locker.read_ue_usage_setting();
        if (v < 0) {
            printf("%s[FAIL]%s Could not read ue_usage_setting (errno=%d)\n",
                   RED, RESET, locker.last_errno());
            client.stop();
            return 1;
        }
        printf("%sue_usage_setting%s = %d (%s)\n", BOLD, RESET, v,
               v == 0   ? "data-centric — works without SIM"
               : v == 1 ? "voice-centric — needs SIM/IMS"
                        : "unknown");
        client.stop();
        return 0;
    }
    if (opts.force_data_centric) {
        printf("%sSwitching modem to data-centric mode%s "
               "(enables SIM-less PLMN registration)...\n",
               BOLD, RESET);
        int r = locker.force_data_centric();
        printf("  data-centric: %s (errno=%d)\n",
               r == 0 ? "OK" : "FAILED", locker.last_errno());
        if (r == 0) {
            printf("%sIMPORTANT:%s reboot modem (-r) for the change to take effect.\n",
                   BOLD, RESET);
        }
        client.stop();
        return r == 0 ? 0 : 1;
    }
    if (opts.restore_voice_centric) {
        printf("%sRestoring voice-centric mode%s (default for smartphones)...\n",
               BOLD, RESET);
        int r = locker.restore_voice_centric();
        printf("  voice-centric: %s (errno=%d)\n",
               r == 0 ? "OK" : "FAILED", locker.last_errno());
        if (r == 0) {
            printf("%sIMPORTANT:%s reboot modem (-r) for the change to take effect.\n",
                   BOLD, RESET);
        }
        client.stop();
        return r == 0 ? 0 : 1;
    }

    if (opts.asn_selftest) {
#ifdef USE_ASN1C_LTE
        return lte_asn1::selftest() ? 0 : 1;
#else
        std::fprintf(stderr, "asn1c not compiled in\n");
        return 1;
#endif
    }
    if (opts.reboot) {
        printf("[REBOOT MODEM...\n");
        client.rebootModem();
        printf("Stopping client...\n");
        client.stop();
        return 0;
    }
    if (!opts.lock_earfcn_pci.empty()) {
        if (opts.lock_mode.empty()) {
            fprintf(stderr, "%s[FAIL]%s -m <2g|3g|4g> is required with -L\n", RED, RESET);
            client.stop();
            return 1;
        }

        // Parse target list — supports both "freq:id" (single, backward-compat)
        // and CSV "f1:p1,f2:p2,..." (batch). Batch is LTE/v3 only; other modes
        // and versions fall back to using only the first pair.
        std::vector<std::pair<uint32_t, uint16_t>> pairs;
        if (!parse_lock_targets(opts.lock_earfcn_pci, pairs)) {
            fprintf(stderr, "ERROR: -L format is freq:id (e.g. -L 38100:455) "
                            "or CSV f1:p1,f2:p2,... for LTE batch lock\n");
            client.stop();
            return 1;
        }

        // For non-batch use: take just the first pair into left/right
        uint32_t left = pairs.front().first;
        uint16_t right = pairs.front().second;

        if (pairs.size() > 1 && (opts.lock_mode != "4g" ||
                                 (opts.lock_version != "v3" && !opts.lock_version.empty()))) {
            fprintf(stderr, "%s[WARN]%s -L: %zu pairs given but mode=%s ver=%s "
                            "doesn't support batch — using only first pair (%u:%u)\n",
                    BOLD, RESET, pairs.size(),
                    opts.lock_mode.c_str(),
                    opts.lock_version.empty() ? "default" : opts.lock_version.c_str(),
                    left, right);
        }

        printf("%sLock request%s mode=%s version=%s",
               BOLD, RESET,
               opts.lock_mode.c_str(),
               opts.lock_version.empty() ? "v1" : opts.lock_version.c_str());
        if (pairs.size() > 1) {
            printf(" batch=%zu cells:\n", pairs.size());
            for (auto &[f, p]: pairs) printf("    %u:%u\n", f, p);
        } else {
            printf(" target=%u:%u\n", left, right);
        }

        bool success = false;
        int ret = -1;

        if (opts.lock_mode == "4g") {
            if (opts.lock_version.empty() || opts.lock_version == "v1") {
                ret = locker.lock_lte_v1(static_cast<uint16_t>(left), right);
            } else if (opts.lock_version == "v2") {
                ret = locker.lock_lte_v2(left, right);
            } else if (opts.lock_version == "v3") {
                // ── BATCH-AWARE: fill BtsLteLockList with all pairs ──
                BtsLteLockList list{};
                size_t n = pairs.size();
                if (n > 30) {
                    fprintf(stderr, "%s[WARN]%s -L: %zu pairs > 30 (cell_lock_list max), truncating\n",
                            BOLD, RESET, n);
                    n = 30;
                }
                list.len = static_cast<uint32_t>(n);
                for (size_t i = 0; i < n; ++i) {
                    list.cell[i].freq = pairs[i].first;
                    list.cell[i].pci = pairs[i].second;
                }
                ret = locker.lock_lte_v3(list);
            } else {
                fprintf(stderr, "%s[FAIL]%s Unknown 4g version: %s\n",
                        RED, RESET, opts.lock_version.c_str());
                client.stop();
                return 1;
            }
        } else if (opts.lock_mode == "3g") {
            if (opts.lock_version.empty() || opts.lock_version == "v1") {
                ret = locker.lock_umts_by_freq(static_cast<uint16_t>(left));
            } else if (opts.lock_version == "v2") {
                ret = locker.lock_umts(right, left);
            } else {
                fprintf(stderr, "%s[FAIL]%s Unknown 3g version: %s\n",
                        RED, RESET, opts.lock_version.c_str());
                client.stop();
                return 1;
            }
        } else if (opts.lock_mode == "2g") {
            if (opts.lock_version.empty() || opts.lock_version == "v1") {
                ret = locker.lock_gsm_arfcns({static_cast<uint16_t>(left)});
            } else {
                fprintf(stderr, "%s[FAIL]%s Unknown 2g version: %s\n",
                        RED, RESET, opts.lock_version.c_str());
                client.stop();
                return 1;
            }
        } else {
            fprintf(stderr, "%s[FAIL]%s Unknown lock mode: %s\n",
                    RED, RESET, opts.lock_mode.c_str());
            client.stop();
            return 1;
        }

        success = (ret == 0);

        if (success) {
            // Mark lock as applied — signal handler (Ctrl-C) will now clean
            // up if user aborts. NOTE: deliberately NOT cleared on normal
            // exit below — `--lock` is a one-shot mode where the user WANTS
            // the lock to persist (they'll call --unlock when done).
            g_lock_applied.store(true);
            printf("%s[OK]%s Lock written.\n", GREEN, RESET);
            printf("  To activate: toggle airplane mode or AT+CFUN=0 then AT+CFUN=1\n");
            printf("  To remove:   %s --unlock\n", argv[0]);
        } else {
            fprintf(stderr, "%s[FAIL]%s Lock failed (errno=%d)\n",
                    RED, RESET, locker.last_errno());
        }

        client.stop();
        return success ? 0 : 1;
    }


    printf("%s     %s%s\n", DIM,
           opts.duration_sec ? (std::string("stopping in ") +
                                std::to_string(opts.duration_sec) + "s")
                                       .c_str()
                             : "press Ctrl-C to stop",
           RESET);
    fflush(stdout);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(opts.duration_sec > 0 ? opts.duration_sec : INT_MAX);
    auto next_refresh = std::chrono::steady_clock::now() +
                        std::chrono::seconds(opts.refresh_sec);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto now_tp = std::chrono::steady_clock::now();

        if (opts.duration_sec > 0 && now_tp >= deadline) {
            g_running = false;
            break;
        }

        if (now_tp >= next_refresh) {
            next_refresh = now_tp + std::chrono::seconds(opts.refresh_sec);

            std::string sl;
            {
                std::lock_guard<std::mutex> lk(status_mx);
                sl = last_status;
            }

            if (locker.is_lte_locked()) {
                char lbuf[64];
                snprintf(lbuf, sizeof(lbuf), "LOCKED EARFCN=%u PCI=%u | ",
                         locker.locked_earfcn(), locker.locked_pci());
                sl = std::string(lbuf) + sl;
            }

            if (opts.nas_mode) {
                render_nas_screen(nas_log.snapshot(), stats, opts);
            } else {
                auto snap = db.snapshot();
                render_screen(snap, stats, opts, sl);

                if (!opts.json_path.empty()) {
                    write_json(opts.json_path, snap, stats);
                    std::lock_guard<std::mutex> lk(status_mx);
                    last_status = std::string("JSON -> ") + opts.json_path;
                }
            }
        }
    }

    client.stop();

    printf("\n%s%s=== Session Summary ===%s\n", BOLD, CYAN, RESET);
    time_t dur = time(nullptr) - stats.start_time;
    printf(" Duration : %s\n", fmt_duration(dur).c_str());
    printf(" Frames   : %llu (GSM:%llu WCDMA:%llu LTE:%llu UMTS:%llu NR:%llu)\n",
           (unsigned long long) stats.frames_total.load(),
           (unsigned long long) stats.frames_gsm.load(),
           (unsigned long long) stats.frames_wcdma.load(),
           (unsigned long long) stats.frames_lte.load(),
           (unsigned long long) stats.frames_umts.load(),
           (unsigned long long) stats.frames_nr.load());
    printf(" Errors   : %llu  NAS: %llu  RRC: %llu\n",
           (unsigned long long) stats.parse_errors.load(),
           (unsigned long long) stats.nas_messages.load(),
           (unsigned long long) stats.rrc_messages.load());

    // ── 0xB0C0 / RRC diagnostic counters ────────────────────────────────
    // RRC stats (received/decoded counters) are now logged from inside
    // DiagLteRrcParser::parse_rrc_ota via periodic SUMMARY lines and
    // first-packet hex dumps. No top-level call needed here.

    auto final_cells = db.snapshot();
    {
        std::lock_guard<std::mutex> _hl(g_code_hist_mx);
        std::vector<std::pair<uint16_t, uint64_t>> hv(g_code_hist.begin(), g_code_hist.end());
        std::sort(hv.begin(), hv.end(), [](const auto &x, const auto &y) { return x.second > y.second; });
        std::string hs;
        char hb[40];
        for (const auto &kv: hv) {
            std::snprintf(hb, sizeof(hb), "0x%04X=%llu ", kv.first, (unsigned long long) kv.second);
            hs += hb;
        }
        DIAG_LOGI("CODE-HIST received log codes (code=count): %s", hs.c_str());
        if (g_journal) {
            JournalRecord jr;
            jr.t = (double) time(nullptr);
            jr.code = 0;
            jr.rat = "-";
            jr.channel = "CODE-HIST";
            jr.msg_type = "histogram";
            jr.summary = "received log codes (discovery)";
            jr.detail = hs;
            jr.len = 0;
            journal_emit(jr);
        }
    }
    printf("\n%s Cells (%zu):%s\n", BOLD, final_cells.size(), RESET);
    printf("%s %-5s  %-7s %-5s  %-5s %-5s  %-6s  %-10s  %-6s  Seen%s\n",
           BOLD, "RAT", "ARFCN", "PCI", "RSRP", "RSRQ", "MCCMNC", "CID", "TAC", RESET);
    printf("%s %s%s\n", DIM, std::string(78, '-').c_str(), RESET);

    for (const auto &c: final_cells) {
        printf(" %s%s%s%s %-7u %-5u  %4d %4d  %-6s  %-10s  %-6s  %u\n",
               rat_color(c.type), rat_name(c.type), RESET,
               c.serving ? "◄" : " ",
               c.freq, c.id, c.rsrp_rscp, c.rsrq_ecno,
               c.mccmnc_str().c_str(), c.cid_str().c_str(),
               c.taclac_str().c_str(), c.seen_count);
    }

    if (!opts.json_path.empty() && !final_cells.empty()) {
        write_json(opts.json_path, final_cells, stats);
        printf("\n%sJSON -> %s%s\n", DIM, opts.json_path.c_str(), RESET);
    }

    printf("\n");
    return 0;
}
