#include "diag_lte_ml1_parser.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <set>
#include <string>
#include <ctime>
#include "journal.h"

using namespace diag_util;

// ═════════════════════════════════════════════════════════════════════════════
// scat measurement conversions (diagltelogparser.py parse_rsrp/rsrq/rssi):
//   RSRP = -180 + raw*0.0625   RSRQ = -30 + raw*0.0625   RSSI = -110 + raw*0.0625
// We reuse diag_util::ml1_rsrp/rsrq/rssi which apply exactly these + rounding.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// RevWordBits — reproduces scat's
//   bitstring.Bits().join([Bits(uint=w, length=32) for w in words][::-1])
// i.e. reverse the word order, lay each word out big-endian, then index bits
// MSB-first. Verified bit-exact against scat's logic over 500 random vectors.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    struct RevWordBits
    {
        uint8_t be[48]; // up to 12 words × 4 bytes
        int nbytes;
        RevWordBits(const uint8_t *le_words, int nwords)
        {
            if (nwords > 12)
                nwords = 12;
            nbytes = nwords * 4;
            for (int i = 0; i < nwords; ++i)
            {
                uint32_t w = rd32(le_words + (nwords - 1 - i) * 4); // reversed word order
                be[i * 4 + 0] = (w >> 24) & 0xFF;                   // big-endian in word
                be[i * 4 + 1] = (w >> 16) & 0xFF;
                be[i * 4 + 2] = (w >> 8) & 0xFF;
                be[i * 4 + 3] = (w >> 0) & 0xFF;
            }
        }
        uint32_t slice(int a, int b) const
        { // bits [a,b) MSB-first
            uint32_t v = 0;
            for (int i = a; i < b; ++i)
            {
                int by = i >> 3, bit = 7 - (i & 7);
                if (by >= nbytes)
                    break;
                v = (v << 1) | ((be[by] >> bit) & 1u);
            }
            return v;
        }
    };
} // namespace

std::vector<uint16_t> DiagLteMl1Parser::handled_log_codes()
{
    return {
        LOG_LTE_ML1_SERVING_MEAS_EVAL_C, // 0xB17F
        LOG_LTE_ML1_NEIGHBOR_MEAS_C,     // 0xB180
        LOG_LTE_ML1_SCELL_MEAS_RESP_C,   // 0xB193
        LOG_LTE_ML1_SERVING_CELL_INFO_C, // 0xB197
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Packet journal: ML1 measurement logs are non-ASN.1 binary structs, so after
// decoding we emit a human-readable detail (RSRP/RSRQ/RSSI per cell) alongside
// the raw hex. These codes are excluded from main.cpp's generic raw-only emit
// so there is exactly one journal entry per packet (raw + parsed).
// ─────────────────────────────────────────────────────────────────────────────
static void ml1_emit_journal(uint16_t code, const char *label,
                             const std::string &summary, const std::string &detail,
                             const uint8_t *p, size_t plen)
{
    if (!journal_enabled())
        return;
    JournalRecord jr;
    jr.t = static_cast<double>(time(nullptr));
    jr.code = code;
    jr.rat = "LTE";
    jr.channel = label;
    jr.msg_type = label;
    jr.summary = summary;
    jr.detail = detail;
    jr.raw = journal_hex(p, plen);
    jr.len = plen;
    journal_emit(jr);
}

void DiagLteMl1Parser::emit_cell(const LteCell &c)
{
    if (cell_cb_)
        cell_cb_(c);
}

void DiagLteMl1Parser::add_or_update_neighbor(const LteCell &c)
{
    // Anti-garbage safety net: every emitted measurement must be physically
    // plausible. A wrong offset on some unseen firmware version then produces
    // "no cell" instead of a fabricated one.
    if (!valid_lte_earfcn(c.earfcn) || !valid_lte_pci(c.pci))
        return;
    if (!valid_lte_rsrp(c.rsrp))
        return;

    for (auto &n : neighbors_)
    {
        if (n.earfcn == c.earfcn && n.pci == c.pci)
        {
            n.rsrp = c.rsrp;
            n.rsrq = c.rsrq;
            n.rssi = c.rssi;
            if (c.serving)
                n.serving = true;
            if (c.cell_id >= 0)
                n.cell_id = c.cell_id;
            if (c.tac > 0)
                n.tac = c.tac;
            return;
        }
    }
    neighbors_.push_back(c);
}

void DiagLteMl1Parser::report_unsupported(uint16_t code, uint8_t ver,
                                          const uint8_t *p, size_t plen)
{
    // Dump ONCE per (code, version). This is how we stop guessing: the user
    // runs the scanner, this prints the real bytes to logcat (tag DiagParser),
    // sends them over, and we add the exact layout — never an invented offset.
    static std::set<uint32_t> seen;
    uint32_t key = (uint32_t(code) << 8) | ver;
    if (seen.count(key))
        return;
    seen.insert(key);

    DIAG_LOGW("ML1 UNSUPPORTED code=0x%04X ver=%u len=%zu — layout not ported, "
              "no data emitted. Capture the hex below and send it to add the layout.",
              code, ver, plen);
    // Route the bytes through logcat (tag DiagParser) so they are visible via
    //   adb logcat -s DiagParser:D
    // — stderr from a root-spawned diag_scan process is not captured there.
    char tag[40];
    std::snprintf(tag, sizeof(tag), "ML1 UNSUP 0x%04X v%u", code, ver);
    hex_dump(tag, p, plen, 128);
}

// ═════════════════════════════════════════════════════════════════════════════
// Top-level dispatch — payload begins AFTER the 12-byte LogRecord header.
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteMl1Parser::parse(const uint8_t *buf, size_t len)
{
    if (len < sizeof(LogRecord))
        return false;
    const auto *hdr = reinterpret_cast<const LogRecord *>(buf);
    const uint8_t *p = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);
    if (plen < 1)
        return false;

    // ─────────────────────────────────────────────────────────────────────────
    // СЕНЬОРСКИЙ АЛИАС ДЛЯ QUALCOMM X55 / SIM8300 (ПАТЧ ПОДМЕНЫ КОДА)
    // ─────────────────────────────────────────────────────────────────────────
    // Локальная переменная кода, чтобы не мутировать константный буфер памяти
    uint16_t target_code = hdr->code;
    if (target_code == 0xB1E1)
    {
        target_code = LOG_LTE_ML1_SCELL_MEAS_RESP_C; // Перенаправляем 0xB1E1 на обработчик 0xB193
    }
    // ─────────────────────────────────────────────────────────────────────────

    if (debug_)
    {
        DIAG_LOGD("ML1 code=0x%04X plen=%zu ver=%u", target_code, plen, p[0]);
        hex_dump("ML1", p, plen, 80);
    }

    // ── One-time FULL raw dump of 0xB17F and 0xB193 / 0xB1E1 ─────────────────
    {
        static bool dumped_b17f = false, dumped_b193 = false;
        if (target_code == LOG_LTE_ML1_SERVING_MEAS_EVAL_C && !dumped_b17f)
        {
            dumped_b17f = true;
            DIAG_LOGI("=== RAW 0xB17F len=%zu ver=%u (capture this) ===", plen, p[0]);
            hex_dump("B17F-RAW", p, plen, plen);
        }
        // Логгер сработает и на оригинальный 0xB193, и на наш подмененный 0xB1E1!
        else if (target_code == LOG_LTE_ML1_SCELL_MEAS_RESP_C && !dumped_b193)
        {
            dumped_b193 = true;
            DIAG_LOGI("=== RAW 0xB193/0xB1E1 len=%zu ver=%u (capture this) ===", plen, p[0]);
            hex_dump("B193-RAW", p, plen, plen);
        }
    }

    // Крутим switch по нашей подмененной переменной target_code
    switch (target_code)
    {
    case LOG_LTE_ML1_SERVING_MEAS_EVAL_C:
        return parse_serving_meas_eval(p, plen);
    case LOG_LTE_ML1_NEIGHBOR_MEAS_C:
        return parse_neighbor_meas(p, plen);
    case LOG_LTE_ML1_SCELL_MEAS_RESP_C:
        return parse_scell_meas_resp(p, plen);
    case LOG_LTE_ML1_SERVING_CELL_INFO_C:
        return parse_serving_cell_info(p, plen);
    default:
        return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB17F — LTE ML1 Serving Cell Meas & Eval   (scat parse_lte_ml1_scell_meas)
//   v4: <BHHHLLLLLL  @ p[1:32]    v5: <BHLH2xLLLLLL @ p[1:36]
// Bit slices are MSB-first (bitstring semantics):
//   pci         = pci_serv_layer_prio[0:9]   = (16-bit) >> 7
//   meas_rsrp   = meas_rsrp & 0xFFF
//   meas_rsrq   = rsrq[0:10]                 = (32-bit) >> 22
//   meas_rssi   = rssi[10:21]                = ((32-bit) >> 11) & 0x7FF
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteMl1Parser::parse_serving_meas_eval(const uint8_t *p, size_t plen)
{
    uint8_t ver = p[0];
    uint32_t earfcn;
    uint16_t pci_slp;
    uint32_t meas_rsrp_w, rsrq_w, rssi_w;

    if (ver == 4)
    {
        if (plen < 32)
            return false;
        earfcn = rd16(p + 4);
        pci_slp = rd16(p + 6);
        meas_rsrp_w = rd32(p + 8);
        rsrq_w = rd32(p + 16);
        rssi_w = rd32(p + 20);
    }
    else if (ver == 5)
    {
        if (plen < 36)
            return false;
        earfcn = rd32(p + 4);
        pci_slp = rd16(p + 8);
        meas_rsrp_w = rd32(p + 12);
        rsrq_w = rd32(p + 20);
        rssi_w = rd32(p + 24);
    }
    else
    {
        report_unsupported(LOG_LTE_ML1_SERVING_MEAS_EVAL_C, ver, p, plen);
        return false;
    }

    uint16_t pci = static_cast<uint16_t>(pci_slp >> 7) & 0x1FF;

    // 0xB17F's PCI is unreliable on this device (observed stuck at 28-31 for
    // every EARFCN), which breaks the merge-by-(EARFCN,PCI) so the serving RSRP
    // never lands on the real serving cell. If 0xB197 gave us the authoritative
    // serving PCI for this EARFCN, use that instead.
    if (srv_info_pci_ != 0xFFFF && srv_info_earfcn_ == earfcn && pci != srv_info_pci_)
    {
        DIAG_LOGI("ML1 0xB17F PCI %u → %u (corrected from 0xB197)", pci, srv_info_pci_);
        pci = srv_info_pci_;
    }

    LteCell c{};
    c.earfcn = earfcn;
    c.pci = pci;
    c.rsrp = ml1_rsrp(meas_rsrp_w & 0xFFF);
    c.rsrq = ml1_rsrq(rsrq_w >> 22);
    c.rssi = ml1_rssi((rssi_w >> 11) & 0x7FF);
    c.serving = true; // this packet IS the serving cell meas

    DIAG_LOGI("ML1 0xB17F SCell: EARFCN=%u PCI=%u RSRP=%d RSRQ=%d RSSI=%d (v%u)",
              c.earfcn, c.pci, c.rsrp, c.rsrq, c.rssi, ver);

    {
        char sm[96], dt[224];
        std::snprintf(sm, sizeof(sm), "EARFCN=%u PCI=%u RSRP=%d RSRQ=%d RSSI=%d",
                      c.earfcn, c.pci, c.rsrp, c.rsrq, c.rssi);
        std::snprintf(dt, sizeof(dt),
                      "ML1 Serving Cell Meas/Eval (v%u)\n"
                      "  EARFCN = %u   PCI = %u\n"
                      "  RSRP = %d dBm   RSRQ = %d dB   RSSI = %d dBm",
                      ver, c.earfcn, c.pci, c.rsrp, c.rsrq, c.rssi);
        ml1_emit_journal(0xB17F, "ML1 SCell Meas/Eval", sm, dt, p, plen);
    }

    if (valid_lte_earfcn(c.earfcn) && valid_lte_pci(c.pci))
    {
        serving_ = c;              // keep a copy for reference
        add_or_update_neighbor(c); // surfaced via fire_neighbor_update()
        emit_cell(c);
        return true;
    }
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB180 — LTE ML1 Neighbor Measurements      (scat parse_lte_ml1_ncell_meas)
//   v4: <BHHH @ p[1:8] (pos=8)   v5: <BHLL @ p[1:12] (pos=12)
//   n_cells = (q_rxlevmin_n_cells >> 6); each cell 32 bytes, first 28 = <LLLLHHLL
//   n_pci      = val0[0:9]   = val0 >> 23
//   n_rssi_raw = val0[9:20]  = (val0 >> 12) & 0x7FF
//   n_rsrp_raw = val0[20:32] = val0 & 0xFFF
//   n_rsrq_raw = (val2 >> 12) & 0x3FF
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteMl1Parser::parse_neighbor_meas(const uint8_t *p, size_t plen)
{
    uint8_t ver = p[0];
    uint32_t earfcn, qrx_ncells;
    size_t pos;

    if (ver == 4)
    {
        if (plen < 8)
            return false;
        earfcn = rd16(p + 4);
        qrx_ncells = rd16(p + 6);
        pos = 8;
    }
    else if (ver == 5)
    {
        if (plen < 12)
            return false;
        earfcn = rd32(p + 4);
        qrx_ncells = rd32(p + 8);
        pos = 12;
    }
    else
    {
        report_unsupported(LOG_LTE_ML1_NEIGHBOR_MEAS_C, ver, p, plen);
        return false;
    }

    uint32_t n_cells = qrx_ncells >> 6;
    if (n_cells > 16)
        return false; // sanity
    bool any = false;
    std::string nbr_detail;
    char nbr_line[128];

    for (uint32_t i = 0; i < n_cells; ++i)
    {
        size_t off = pos + 32 * i;
        if (off + 28 > plen)
            break;
        uint32_t val0 = rd32(p + off + 0);
        uint32_t val2 = rd32(p + off + 8);

        LteCell c{};
        c.earfcn = earfcn;
        c.pci = static_cast<uint16_t>(val0 >> 23) & 0x1FF;
        c.rsrp = ml1_rsrp(val0 & 0xFFF);
        c.rssi = ml1_rssi((val0 >> 12) & 0x7FF);
        c.rsrq = ml1_rsrq((val2 >> 12) & 0x3FF);
        c.serving = false;

        DIAG_LOGD("ML1 0xB180 NCell[%u]: EARFCN=%u PCI=%u RSRP=%d RSRQ=%d",
                  i, c.earfcn, c.pci, c.rsrp, c.rsrq);
        std::snprintf(nbr_line, sizeof(nbr_line),
                      "  [%u] PCI=%u  RSRP=%d dBm  RSRQ=%d dB  RSSI=%d dBm\n",
                      i, c.pci, c.rsrp, c.rsrq, c.rssi);
        nbr_detail += nbr_line;
        add_or_update_neighbor(c);
        emit_cell(c);
        any = true;
    }
    if (any)
    {
        char sm[64];
        std::snprintf(sm, sizeof(sm), "EARFCN=%u cells=%u", earfcn, n_cells);
        ml1_emit_journal(0xB180, "ML1 NCell Meas", sm,
                         "ML1 Neighbor Measurements  EARFCN=" + std::to_string(earfcn) +
                             "\n" + nbr_detail,
                         p, plen);
    }
    return any;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0xB193 subpkt 0x19 per-cell (scat parse_lte_ml1_scell_meas_response_cell_v36)
//   header (HHH): val0=cell[0:2], val2=cell[4:6]
//     pci      = val0[0:9]  = val0 >> 7 ;  is_serving = val0[12:13] = (val0>>3)&1
//   12 LE words at cell[16:64] → reversed-word MSB-first join (RevWordBits)
//     rsrp_raw = slice[108:120] ; rsrq_raw = slice[224:234] ; rssi_raw = slice[320:331]
// NOTE: scat computes these but does not print them, so the bit offsets are
// unverified end-to-end. We emit them anyway BUT every value passes through the
// validity clamp in add_or_update_neighbor — a wrong offset yields no cell, not
// a fake one. Compare these against 0xB17F / RRC / AT+CSQ on-device to confirm.
// ─────────────────────────────────────────────────────────────────────────────
bool DiagLteMl1Parser::scell_resp_cell_v36(int idx, const uint8_t *cell, size_t clen,
                                           uint32_t earfcn, bool &serving_out)
{
    if (clen < 64)
        return false;
    uint16_t val0 = rd16(cell + 0);
    uint16_t pci = static_cast<uint16_t>(val0 >> 7) & 0x1FF;
    bool is_scell = ((val0 >> 3) & 1u) != 0;
    serving_out = is_scell;

    RevWordBits rb(cell + 16, 12);
    LteCell c{};
    c.earfcn = earfcn;
    c.pci = pci;
    c.rsrp = ml1_rsrp(rb.slice(108, 120));
    c.rsrq = ml1_rsrq(rb.slice(224, 234));
    c.rssi = ml1_rssi(rb.slice(320, 331));
    c.serving = is_scell;

    DIAG_LOGD("ML1 0xB193 cell[%d]: EARFCN=%u PCI=%u RSRP=%d RSRQ=%d%s (EXPERIMENTAL)",
              idx, c.earfcn, c.pci, c.rsrp, c.rsrq, is_scell ? " serving" : "");

    if (is_scell && valid_lte_earfcn(c.earfcn) && valid_lte_pci(c.pci))
        serving_ = c;
    add_or_update_neighbor(c);
    emit_cell(c);
    return true;
}

bool DiagLteMl1Parser::scell_resp_cell_v48(int idx, const uint8_t *cell, size_t clen,
                                           uint32_t earfcn, bool &serving_out)
{
    // v48/v50 cell is 140 bytes; the header + RSRP/RSRQ/RSSI bit layout used
    // here (offset 16, 12 words) is identical to v36 — only SNR/CINR offsets
    // (which we do not extract) differ.
    return scell_resp_cell_v36(idx, cell, clen, earfcn, serving_out);
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB193 — LTE ML1 Serving Cell Meas Response (scat parse_lte_ml1_scell_meas_response)
//   pkt_version must be 1. header: ver(1) num_subpkts(1) reserved(2), pos=4.
//   subpkt header <BBH: id, version, size. (scat advances pos += size)
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteMl1Parser::parse_scell_meas_resp(const uint8_t *p, size_t plen)
{
    uint8_t pkt_ver = p[0];
    if (pkt_ver != 1)
    {
        report_unsupported(LOG_LTE_ML1_SCELL_MEAS_RESP_C, pkt_ver, p, plen);
        return false;
    }
    if (plen < 4)
        return false;
    uint8_t num_subpkts = p[1];
    size_t pos = 4;
    bool any = false;
    uint32_t j_earfcn = 0;
    uint16_t j_cells = 0;
    uint8_t j_ver = 0;
    bool j_have = false;

    for (uint8_t s = 0; s < num_subpkts; ++s)
    {
        if (pos + 4 > plen)
            break;
        uint8_t id = p[pos + 0];
        uint8_t sver = p[pos + 1];
        uint16_t size = rd16(p + pos + 2); // TOTAL subpkt size (4B hdr + body)
        const uint8_t *body = p + pos + 4;
        if (size < 4 || pos + size > plen)
            break;
        size_t blen = static_cast<size_t>(size) - 4; // body length
        pos += size;                                 // scat semantics

        if (id != LTE_ML1_Serving_Cell_Meas_Results)
        {
            if (debug_)
                DIAG_LOGD("ML1 0xB193 subpkt id=0x%02X ignored", id);
            continue;
        }

        if (sver == 36)
        {
            if (blen < 8)
                continue;
            uint32_t earfcn = rd32(body + 0);
            uint16_t num_cells = rd16(body + 4);
            j_earfcn = earfcn;
            j_cells = num_cells;
            j_ver = sver;
            j_have = true;
            size_t m = 8;
            for (uint16_t y = 0; y < num_cells; ++y)
            {
                if (m + 128 > blen)
                    break;
                bool sv = false;
                if (scell_resp_cell_v36(y, body + m, 128, earfcn, sv))
                    any = true;
                m += 128;
            }
        }
        else if (sver == 48 || sver == 50)
        {
            if (blen < 12)
                continue;
            uint32_t earfcn = rd32(body + 0);
            uint16_t num_cells = rd16(body + 4);
            j_earfcn = earfcn;
            j_cells = num_cells;
            j_ver = sver;
            j_have = true;
            size_t m = 12;
            for (uint16_t y = 0; y < num_cells; ++y)
            {
                if (m + 140 > blen)
                    break;
                bool sv = false;
                if (scell_resp_cell_v48(y, body + m, 140, earfcn, sv))
                    any = true;
                m += 140;
            }
        }
        else if (sver == 59)
        {
            // v59 (SM8550): header is the v48 12-byte form (verified on two
            // captures — earfcn[0:4], num_cells[4:6], valid_rx[6:8], rx_map
            // [8:12]). The PER-CELL measurement layout, however, does NOT match
            // scat's v36/v48 offsets (PCI/RSRP don't land), so we do NOT decode
            // cells here — guessing offsets is exactly what produced garbage
            // before. The serving RSRP/RSRQ/RSSI is already obtained from 0xB17F
            // (with the 0xB197 PCI correction), so nothing is lost for the
            // serving cell. If a multi-cell v59 ever appears, dump it once so we
            // can derive the real cell layout from data, not guesswork.
            if (blen < 12)
                continue;
            uint32_t earfcn = rd32(body + 0);
            uint16_t num_cells = rd16(body + 4);
            uint32_t rx_map = rd32(body + 8);
            j_earfcn = earfcn;
            j_cells = num_cells;
            j_ver = sver;
            j_have = true;
            (void)rx_map;
            if (debug_)
                DIAG_LOGD("ML1 0xB193 v59: EARFCN=%u cells=%u rx_map=0x%X "
                          "(serving signal via 0xB17F; cell layout unverified)",
                          earfcn, num_cells, rx_map);
            if (num_cells > 1)
            {
                static bool dumped_multi = false;
                if (!dumped_multi)
                {
                    dumped_multi = true;
                    DIAG_LOGI("=== 0xB193 v59 MULTI-CELL (cells=%u) — capture to add layout ===",
                              num_cells);
                    hex_dump("B193v59-MULTI", p, plen, plen);
                }
            }
            // header recognized → not "unsupported"; no cell emit (no guessing)
        }
        else
        {
            // v60+ etc — scat itself leaves these unimplemented. Fail closed.
            report_unsupported(LOG_LTE_ML1_SCELL_MEAS_RESP_C,
                               static_cast<uint8_t>(0x80 | sver), body, blen);
        }
    }
    if (j_have)
    {
        char sm[96], dt[192];
        std::snprintf(sm, sizeof(sm), "v%u EARFCN=%u cells=%u", j_ver, j_earfcn, j_cells);
        std::snprintf(dt, sizeof(dt),
                      "ML1 Serving Cell Meas Response (subpkt v%u)\n"
                      "  EARFCN = %u   cells = %u\n"
                      "  (serving RSRP/RSRQ via 0xB17F; per-cell v59 layout unverified)",
                      j_ver, j_earfcn, j_cells);
        ml1_emit_journal(0xB193, "ML1 SCell Meas Resp", sm, dt, p, plen);
    }
    return any;
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB197 — LTE ML1 Serving Cell Information   (scat parse_lte_ml1_cell_info)
//   v1: <BHHHLLQLhH @ p[1:32]   v2: <BHLLLLQLhH @ p[1:36]
//   pci = (pci_pbch_phich & 0xFFFF)[0:9] = ((pci_pbch_phich & 0xFFFF) >> 7)
// Provides serving EARFCN/PCI (+ bandwidth/MIB we don't currently store).
// Used to anchor the serving identity; carries no RSRP, so we only update the
// serving reference and do not push a measurement cell.
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteMl1Parser::parse_serving_cell_info(const uint8_t *p, size_t plen)
{
    uint8_t ver = p[0];
    uint32_t earfcn, pci_pbch_phich;

    if (ver == 1)
    {
        if (plen < 32)
            return false;
        earfcn = rd16(p + 4);
        pci_pbch_phich = rd16(p + 6);
    }
    else if (ver == 2)
    {
        if (plen < 36)
            return false;
        earfcn = rd32(p + 4);
        pci_pbch_phich = rd32(p + 8);
    }
    else
    {
        report_unsupported(LOG_LTE_ML1_SERVING_CELL_INFO_C, ver, p, plen);
        return false;
    }

    uint16_t pci = static_cast<uint16_t>((pci_pbch_phich & 0xFFFF) >> 7) & 0x1FF;
    if (!valid_lte_earfcn(earfcn) || !valid_lte_pci(pci))
        return false;

    DIAG_LOGI("ML1 0xB197 ServInfo: EARFCN=%u PCI=%u (v%u)", earfcn, pci, ver);
    {
        char sm[64], dt[128];
        std::snprintf(sm, sizeof(sm), "EARFCN=%u PCI=%u", earfcn, pci);
        std::snprintf(dt, sizeof(dt),
                      "ML1 Serving Cell Information (v%u)\n  EARFCN = %u   PCI = %u",
                      ver, earfcn, pci);
        ml1_emit_journal(0xB197, "ML1 SCell Info", sm, dt, p, plen);
    }
    serving_.earfcn = earfcn;
    serving_.pci = pci;
    // Remember the authoritative serving PCI so 0xB17F's RSRP attaches to the
    // correct cell (0xB17F's own PCI is unreliable on this chip).
    srv_info_earfcn_ = earfcn;
    srv_info_pci_ = pci;
    // Nudge the merge/UI so the serving identity propagates even before RSRP.
    emit_cell(serving_);
    return true;
}
