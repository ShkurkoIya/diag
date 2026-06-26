#include "diag_wcdma_log_parser.h"
#include "asn1c_umts_bridge.h"
#include "journal.h"
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace diag_util;

// ─────────────────────────────────────────────────────────────────────────────
// WCDMA parser — scat-verified layouts only.
//
// Reliable sources (ported from scat/diagwcdmalogparser.py):
//   0x4027 WCDMA Cell ID      → serving cell identity (UARFCN/PSC/CID/LAC/MCC/MNC)
//   0x4005 Search Cell Resel  → per-cell UARFCN/PSC + RSCP/EcNo (versioned, v0/v1/v2)
//
// Unverified / fail-closed (scat does not decode these — layout varies by fw):
//   0x4127 Serving Cell Info  → one-time hex dump, no emit
//   0x4111 Neighbor Set       → one-time hex dump, no emit
//
// Project principle: never guess struct offsets. The 0x4005 path alone
// already gives serving + neighbor RSCP/EcNo, so 0x4127/0x4111 are not
// load-bearing.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

    // Clamp raw dBm to a sane WCDMA RSCP range. After scat's `rscp - 21`
    // adjustment, values land in roughly -120..-25 dBm.
    inline int16_t clamp_rscp(int raw) {
        if (raw < -140) return -140;
        if (raw > 0) return 0;
        return static_cast<int16_t>(raw);
    }

    inline int16_t clamp_ecno(int raw) {
        if (raw < -50) return -50;
        if (raw > 0) return 0;
        return static_cast<int16_t>(raw);
    }

    // Decode 3 bytes BCD into a small integer.
    // Each byte's LOW nibble carries one digit; HIGH nibble is filler (0xF) in
    // Qualcomm's `3s` packing. We stop at the first filler digit so a 2-digit
    // MNC doesn't get an extra trailing zero appended.
    inline uint32_t bcd3_to_int(const uint8_t *p) {
        uint32_t r = 0;
        for (int i = 0; i < 3; ++i) {
            uint8_t d = static_cast<uint8_t>(p[i] & 0x0F);
            if (d == 0xF) break;
            if (d > 9) continue;
            r = r * 10 + d;
        }
        return r;
    }

}// namespace

int16_t DiagWcdmaLogParser::raw_rscp_to_dbm(int16_t raw) { return clamp_rscp(raw); }
int16_t DiagWcdmaLogParser::raw_ecno_to_db(int16_t raw) { return clamp_ecno(raw); }

std::vector<uint16_t> DiagWcdmaLogParser::handled_log_codes() {
    return {
            LOG_WCDMA_SEARCH_CELL_RESELECTION_RANK_C,// 0x4005 — signal source
            LOG_WCDMA_CELL_ID_C,                     // 0x4027 — identity source
            LOG_WCDMA_NEIGHBOR_SET_C,                // 0x4111 — unverified, hex dump
            LOG_WCDMA_SERVING_CELL_INFO_C,           // 0x4127 — unverified, hex dump
            LOG_WCDMA_RRC_OTA_C,                     // 0x412F — UMTS RRC signaling (asn1c)
    };
}

bool DiagWcdmaLogParser::parse(const uint8_t *buf, size_t len) {
    if (len < sizeof(LogRecord)) {
        DIAG_LOGW("WCDMA: packet too short (%zu)", len);
        return false;
    }
    const auto *hdr = reinterpret_cast<const LogRecord *>(buf);
    const uint8_t *p = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);

    switch (hdr->code) {
        case LOG_WCDMA_CELL_ID_C:
            return parse_cell_id(p, plen);
        case LOG_WCDMA_SEARCH_CELL_RESELECTION_RANK_C:
            return parse_resel_rank(p, plen);
        case LOG_WCDMA_SERVING_CELL_INFO_C:
            return parse_serving_cell(p, plen);
        case LOG_WCDMA_NEIGHBOR_SET_C:
            return parse_neighbor_set(p, plen);
        case LOG_WCDMA_RRC_OTA_C:
            return parse_rrc_ota(p, plen);
        default:
            return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x4027 — WCDMA Cell ID (scat parse_wcdma_cell_id, layout '<LL LH BB H 3s 3s LL')
//
//   off  size  field
//    0    4    ul_uarfcn   (u32 LE)
//    4    4    dl_uarfcn   (u32 LE)
//    8    4    cell_id     (u32 LE — 28-bit UTRAN CID, top 4 bits reserved)
//   12    2    ura_id      (u16 LE)
//   14    1    flags
//   15    1    access
//   16    2    psc_raw     (u16 LE — REAL PSC = psc_raw >> 4)
//   18    3    mcc         (BCD, each byte's low nibble = one digit)
//   21    3    mnc         (BCD, each byte's low nibble = one digit)
//   24    4    lac         (u32 LE)
//   28    4    rac         (u32 LE)
// total = 32 bytes
//
// This gives the serving cell's identity. Signal (RSCP/EcNo) does NOT come
// from here — it comes from 0x4005.
// ─────────────────────────────────────────────────────────────────────────────
bool DiagWcdmaLogParser::parse_cell_id(const uint8_t *p, size_t plen) {
    if (plen < 32) {
        DIAG_LOGW("WCDMA 0x4027: short payload (%zu < 32)", plen);
        return false;
    }
    uint32_t ul_uarfcn = rd32(p + 0);
    uint32_t dl_uarfcn = rd32(p + 4);
    uint32_t cell_id = rd32(p + 8) & 0x0FFFFFFFu;// 28-bit
    uint16_t psc_raw = rd16(p + 16);
    uint16_t psc = static_cast<uint16_t>(psc_raw >> 4);
    uint32_t mcc = bcd3_to_int(p + 18);
    uint32_t mnc = bcd3_to_int(p + 21);
    uint32_t lac = rd32(p + 24);

    // Update serving cell identity. Preserve any RSCP/EcNo already gathered
    // from a prior 0x4005 (the parser is stateful within one DIAG session).
    serving_.uarfcn = static_cast<uint16_t>(dl_uarfcn);
    serving_.psc = psc;
    serving_.cid = cell_id;
    serving_.lac = static_cast<uint16_t>(lac);
    serving_.mcc = mcc;
    serving_.mnc = mnc;
    serving_.serving = true;

    DIAG_LOGI("WCDMA 0x4027 CellId: UARFCN dl=%u ul=%u PSC=%u CID=%u LAC=%u MCC=%u MNC=%u",
              dl_uarfcn, ul_uarfcn, psc, cell_id, lac, mcc, mnc);
    if (cell_cb_) cell_cb_(serving_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x4005 — WCDMA Search Cell Reselection Rank (scat parse_wcdma_search_cell_reselection)
//
// Header (2 bytes, plus 5 padding for v2):
//   byte 0: bits[7:6] = pkt_version (0/1/2), bits[5:0] = num_wcdma_cells
//   byte 1: bits[5:0] = num_gsm_cells
//   (v2 only: 5 bytes padding follow)
//
// Per-3G-cell record (variable size by version):
//   v0 (10b): <HHbhbh  = uarfcn psc rscp_raw rank_rscp ecio_raw rank_ecio
//   v1 (11b):  + s8 resel_status
//   v2 (16b):  + s8 resel_status + s16 hcs_priority + s16 h_value + s8 hcs_cell_qualify
//
// Per-2G-cell record (we skip these — GSM parser handles 2G cells directly):
//   v0 7b, v1 8b, v2 13b
//
// Conversions (scat-verified):
//   real_rscp_dBm = rscp_raw - 21
//   real_ecio_dB  = ecio_raw / 2
// ─────────────────────────────────────────────────────────────────────────────
bool DiagWcdmaLogParser::parse_resel_rank(const uint8_t *p, size_t plen) {
    if (plen < 2) return false;

    uint8_t b0 = p[0];
    uint8_t ver = static_cast<uint8_t>((b0 >> 6) & 0x3);
    uint8_t num_3g = static_cast<uint8_t>(b0 & 0x3F);
    uint8_t num_2g = static_cast<uint8_t>(p[1] & 0x3F);

    if (ver > 2) {
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            DIAG_LOGW("WCDMA 0x4005: unsupported version %u — dumping once", ver);
            hex_dump("WCDMA-4005-UNSUP", p, plen, plen);
        }
        return false;
    }

    size_t per_3g = (ver == 0) ? 10 : (ver == 1) ? 11
                                                 : 16;
    size_t pos = (ver == 2) ? 7 : 2;// v2 has 5 extra padding bytes

    DIAG_LOGI("WCDMA 0x4005 v%u: 3G=%u 2G=%u (plen=%zu)", ver, num_3g, num_2g, plen);

    for (uint8_t i = 0; i < num_3g; ++i) {
        if (pos + per_3g > plen) {
            DIAG_LOGW("WCDMA 0x4005: truncated at 3G cell %u/%u", i, num_3g);
            break;
        }
        uint16_t uarfcn = rd16(p + pos + 0);
        uint16_t psc = rd16(p + pos + 2);
        // pos+4 is signed 8-bit rscp_raw; pos+5..6 is rank_rscp (s16, ignored);
        // pos+7 is signed 8-bit ecio_raw; pos+8..9 is rank_ecio (s16, ignored).
        int8_t rscp_raw = static_cast<int8_t>(p[pos + 4]);
        int8_t ecio_raw = static_cast<int8_t>(p[pos + 7]);

        int16_t rscp_dbm = clamp_rscp(static_cast<int>(rscp_raw) - 21);
        int16_t ecno_db = clamp_ecno(static_cast<int>(ecio_raw) / 2);

        // If this matches our serving cell identity (set by 0x4027), update
        // its signal and re-emit. Otherwise treat as a neighbor.
        bool is_serving =
                (serving_.uarfcn != 0 && serving_.uarfcn == uarfcn && serving_.psc == psc);

        if (is_serving) {
            serving_.rscp = rscp_dbm;
            serving_.ecno = ecno_db;
            DIAG_LOGD("WCDMA 0x4005[%u]: SERVING UARFCN=%u PSC=%u RSCP=%d EcNo=%d",
                      i, uarfcn, psc, rscp_dbm, ecno_db);
            if (cell_cb_) cell_cb_(serving_);
        } else {
            // Merge by (uarfcn, psc) into neighbors_; add if new.
            bool merged = false;
            for (auto &n: neighbors_) {
                if (n.uarfcn == uarfcn && n.psc == psc) {
                    n.rscp = rscp_dbm;
                    n.ecno = ecno_db;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                WcdmaCell cell{};
                cell.uarfcn = uarfcn;
                cell.psc = psc;
                cell.rscp = rscp_dbm;
                cell.ecno = ecno_db;
                cell.serving = false;
                neighbors_.push_back(cell);
                if (cell_cb_) cell_cb_(cell);
            }
            DIAG_LOGD("WCDMA 0x4005[%u]: NBR     UARFCN=%u PSC=%u RSCP=%d EcNo=%d",
                      i, uarfcn, psc, rscp_dbm, ecno_db);
        }
        pos += per_3g;
    }

    // 2G entries follow — GSM cells observed during WCDMA cell search. We
    // intentionally don't emit them: the GSM parser owns 2G; doing it here
    // would produce duplicates with weaker/older data. Just advance pos for
    // any future per-packet stats and return.
    (void) num_2g;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x4127 — WCDMA Serving Cell Info  (device-verified on Xiaomi SM8550)
//
// scat does NOT decode this code, but on this firmware 0x4127 carries the
// same 32-byte identity record as scat's 0x4027 (the "Cell ID" log) with TWO
// differences:
//   1. PSC at offset 16 is stored RAW (no `>> 4` shift).
//   2. The MCC/MNC byte arrays at 18..23 are per-digit (one digit per byte,
//      low nibble; high nibble = 0xF for filler / 2-digit MNC).
//
// Verified end-to-end on one capture (Tele2 RU after operator switch):
//   ul_uarfcn=9613  dl_uarfcn=10563  cell_id=4509501  PSC=170
//   MCC=250 (RU)    MNC=20 (Tele2)   LAC=19701       RAC=223
// All values cross-checked against simultaneous 0x4005 neighbor list.
//
// Bounds-validation rejects the record if MCC/MNC are out of range — that
// way a future firmware with a different layout fails closed instead of
// emitting garbage.
// ─────────────────────────────────────────────────────────────────────────────
bool DiagWcdmaLogParser::parse_serving_cell(const uint8_t *p, size_t plen) {
    if (plen < 32) {
        DIAG_LOGW("WCDMA 0x4127: short payload (%zu < 32)", plen);
        return false;
    }
    uint32_t ul_uarfcn = rd32(p + 0);
    uint32_t dl_uarfcn = rd32(p + 4);
    uint32_t cell_id = rd32(p + 8) & 0x0FFFFFFFu;// 28-bit UTRAN CID
    uint16_t psc = rd16(p + 16);                 // RAW (no >>4 on this fw)
    uint32_t mcc = bcd3_to_int(p + 18);
    uint32_t mnc = bcd3_to_int(p + 21);
    uint32_t lac_u32 = rd32(p + 24);
    uint16_t lac = static_cast<uint16_t>(lac_u32 & 0xFFFF);
    // u32 rac at +28 (unused for now — not exposed in WcdmaCell)

    // Sanity-validate: bad layout produces out-of-range values; reject those.
    if (mcc == 0 || mcc > 999 || mnc > 999 || psc > 511) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            DIAG_LOGW("WCDMA 0x4127: rejected — MCC=%u MNC=%u PSC=%u (layout differs on this fw?)",
                      mcc, mnc, psc);
            hex_dump("WCDMA-4127-REJECT", p, plen, plen);
        }
        return false;
    }

    serving_.uarfcn = static_cast<uint16_t>(dl_uarfcn);
    serving_.psc = psc;
    serving_.cid = cell_id;
    serving_.lac = lac;
    serving_.mcc = mcc;
    serving_.mnc = mnc;
    serving_.serving = true;

    DIAG_LOGI("WCDMA 0x4127 Serving: UARFCN dl=%u ul=%u PSC=%u CID=%u LAC=%u MCC=%u MNC=%u",
              dl_uarfcn, ul_uarfcn, psc, cell_id, lac, mcc, mnc);
    if (cell_cb_) cell_cb_(serving_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x4111 — WCDMA Neighbor Set / Monitored Set (device-verified on SM8550)
//
// Layout (verified on one capture, 172 bytes for num_cells=24):
//   byte 0:    u8  version
//   bytes 1-2: u16 LE UARFCN
//   byte 3:    u8  num_cells
//   N × 7 bytes: u16 PSC + 5 bytes filler (0x00 + 0xFF*4 in observed data)
//
// This is the "monitored set" — PSCs the modem watches for measurement. It
// duplicates the PSC list in 0x4005 (which carries signal) so we don't emit
// these as cells; that would add 24 zero-signal rows per scan and clutter
// the table. We just log the size + a few PSCs for diagnostics.
// ─────────────────────────────────────────────────────────────────────────────
bool DiagWcdmaLogParser::parse_neighbor_set(const uint8_t *p, size_t plen) {
    if (plen < 4) return false;
    uint8_t version = p[0];
    uint16_t uarfcn = rd16(p + 1);
    uint8_t num_cells = p[3];

    size_t need = 4 + static_cast<size_t>(num_cells) * 7;
    if (plen < need) {
        DIAG_LOGW("WCDMA 0x4111: short — need %zu have %zu (n=%u)", need, plen, num_cells);
        // Fall through and parse what we can.
    }

    // Brief PSC dump (up to 8) for diagnostics; full list rarely useful.
    char tail[128];
    size_t off = 0;
    uint8_t shown = num_cells < 8 ? num_cells : 8;
    for (uint8_t i = 0; i < shown; ++i) {
        size_t entry_off = 4 + static_cast<size_t>(i) * 7;
        if (entry_off + 2 > plen) break;
        uint16_t psc = rd16(p + entry_off);
        int w = std::snprintf(tail + off, sizeof(tail) - off, "%s%u",
                              i ? "," : "", psc);
        if (w < 0 || static_cast<size_t>(w) >= sizeof(tail) - off) break;
        off += static_cast<size_t>(w);
    }
    DIAG_LOGI("WCDMA 0x4111 MonSet v%u: UARFCN=%u cells=%u psc=[%s%s]",
              version, uarfcn, num_cells, tail,
              num_cells > shown ? ",…" : "");
    // Do not emit cells — 0x4005 already covers PSCs that get measured.
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x412F — WCDMA RRC signaling (OTA). Header = struct diag_umts_rrc_msg:
//   off 0  u8   chan_type  (0 UL-CCCH,1 UL-DCCH,2 DL-CCCH,3 DL-DCCH,
//                           4 BCCH-BCH,5 BCCH-FACH,6 PCCH)
//   off 1  u8   rb_id
//   off 2  u16  length     (LE — RRC PER payload length)
//   off 4  ..   msg[length]
// UMTS RRC (25.331) is UNALIGNED PER → decoded via the asn1c UMTS bridge.
// ─────────────────────────────────────────────────────────────────────────────
// Pull the camped cell's UTRAN Cell Identity out of a decoded SIB3 segment's
// text tree. Finds the first "sib-Type: 3" segment, reads the first 4 hex bytes
// of its "sib-Data-variable:" line, and takes bits 1..28 (cellIdentity). Returns
// 0 if no usable SIB3 segment is present.
static uint32_t extract_sib3_cid_from_tree(const std::string &tree) {
    size_t p = tree.find("sib-Type: 3 ");
    while (p != std::string::npos) {
        size_t d = tree.find("sib-Data-variable:", p);
        if (d == std::string::npos) break;
        d += std::strlen("sib-Data-variable:");
        size_t eol = tree.find('\n', d);
        std::string line = tree.substr(d, (eol == std::string::npos ? tree.size() : eol) - d);
        uint8_t b[4];
        int n = 0;
        unsigned v = 0;
        int nib = 0;
        for (char c: line) {
            if (c == '(') break;// stop at "(N bits unused)"
            int h = (c >= '0' && c <= '9')   ? c - '0'
                    : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                    : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                                             : -1;
            if (h < 0) {
                if (nib) break;
                else
                    continue;
            }
            v = (v << 4) | h;
            nib++;
            if (nib == 2) {
                b[n++] = static_cast<uint8_t>(v);
                v = 0;
                nib = 0;
                if (n == 4) break;
            }
        }
        if (n == 4) {
            uint32_t w = (uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 |
                         (uint32_t) b[2] << 8 | b[3];
            uint32_t cid = (w >> 3) & 0x0FFFFFFFu;// drop sib4indicator bit, take 28
            if (cid != 0 && cid != 0x0FFFFFFFu) return cid;
        }
        p = tree.find("sib-Type: 3 ", p + 1);
    }
    return 0;
}

bool DiagWcdmaLogParser::parse_rrc_ota(const uint8_t *p, size_t plen) {
    if (plen < 4) return false;
    uint8_t chan = p[0];
    uint8_t rb = p[1];
    uint16_t length = static_cast<uint16_t>(p[2] | (p[3] << 8));
    const uint8_t *msg = p + 4;
    if (length > plen - 4) length = static_cast<uint16_t>(plen - 4);

    // Decode the RRC PDU to text when journaling OR when we still lack the
    // camped cell's CID (so SIB3 can supply it — see extract_sib3_cid below).
    // Capped so we don't asn1c-decode all ~3000 BCCH PDUs once CID is known.
    static int sib3_tries = 0;
    const bool want_cid = (serving_.cid == 0 && sib3_tries < 400);
    std::string tree, matched;
    if (journal_enabled() || want_cid) {
        tree = umts_asn1::decode_pdu_text_any(msg, length, chan, matched);
    }

    if (journal_enabled()) {
        char chbuf[24];
        std::snprintf(chbuf, sizeof(chbuf), "RRC ch=0x%02X", chan);
        char sumbuf[64];
        std::snprintf(sumbuf, sizeof(sumbuf), "chan=0x%02X rb=%u len=%u",
                      chan, rb, (unsigned) length);
        JournalRecord jr;
        jr.t = static_cast<double>(time(nullptr));
        jr.code = 0x412F;
        jr.rat = "WCDMA";
        jr.channel = matched.empty() ? std::string(chbuf) : matched;
        jr.msg_type = matched.empty() ? std::string(chbuf) : matched;
        jr.summary = sumbuf;
        jr.raw = journal_hex(msg, length);
        jr.detail = tree;
        jr.len = length;
        journal_emit(jr);
    }

    // ── SIB3 → camped-cell UTRAN Cell Identity (standards-based) ──────────
    // SysInfoType3 has no UPER preamble before cellIdentity (verified: no
    // extension marker, no optional field ahead of it in 25.331), so its
    // encoding opens with sib4indicator(1 bit) + cellIdentity(28-bit fixed).
    // The SIB3 firstSegment therefore starts with those bits, so CID is just
    // (BE32(firstSeg[0:4]) >> 3) & 0x0FFFFFFF — no full SIB reassembly needed.
    // SIB3 is the BCCH of the cell we're camped on, so this is the SERVING
    // cell's CID: an independent cross-check/fallback for the 0x4127 parse.
    if (want_cid && !tree.empty()) {
        sib3_tries++;
        uint32_t cid = extract_sib3_cid_from_tree(tree);
        if (cid != 0) {
            serving_.cid = cid;
            serving_.serving = true;
            DIAG_LOGI("WCDMA SIB3: camped CID=%u (UARFCN=%u PSC=%u)",
                      cid, serving_.uarfcn, serving_.psc);
            if (cell_cb_) cell_cb_(serving_);
        }
    }
    return true;
}
