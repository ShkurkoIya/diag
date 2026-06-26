#pragma once
#ifndef DIAG_LTE_ML1_PARSER_H
#define DIAG_LTE_ML1_PARSER_H

#include "diag_common.h"

// ─────────────────────────────────────────────────────────────────────────────
// LTE ML1 measurement parser  (LOG_LTE_BASE_C = 0xB010)
//
// ★ REWRITE NOTE (qcdm rework) ───────────────────────────────────────────────
// The previous implementation invented field offsets ("estimated per-cell
// block", "tentative", "Heuristic: scan remaining data") and bogus structs
// (RSRP as int16/8). EARFCN landed at offset 0 so it looked right, while
// PCI/RSRP/RSRQ/RSSI were garbage. It also mislabelled the log codes
// (0xB192/0xB193 treated as a generic subpacket container).
//
// This version follows scat (fgsect/scat diagltelogparser.py) byte-for-byte
// and ONLY the codes whose layout scat actually documents:
//
//   0xB17F  Serving Cell Meas & Eval   → serving RSRP/RSRQ/RSSI  (v4, v5)
//   0xB180  Neighbor Measurements      → neighbour RSRP/RSRQ/RSSI(v4, v5)
//   0xB193  Serving Cell Meas Response → subpkt 0x19 per-cell     (v36/48/50)
//   0xB197  Serving Cell Information   → EARFCN/PCI/BW + MIB       (v1, v2)
//
// Hard rule going forward: NO guessed offsets. Any log code or struct version
// we have not verified against scat (or against a real device hex dump) is
// FAIL-CLOSED — we dump it once to logcat (tag DiagParser) and emit nothing,
// rather than fabricating numbers. Every emitted measurement is additionally
// run through valid_lte_*() so a wrong offset produces "no data", never noise.
// ─────────────────────────────────────────────────────────────────────────────

#define LOG_LTE_ML1_SERVING_MEAS_EVAL_C ((uint16_t) (0xB17F))// serving meas+eval
#define LOG_LTE_ML1_NEIGHBOR_MEAS_C ((uint16_t) (0xB180))    // neighbour meas
#define LOG_LTE_ML1_SCELL_MEAS_RESP_C ((uint16_t) (0xB193))  // serving meas response
#define LOG_LTE_ML1_SERVING_CELL_INFO_C ((uint16_t) (0xB197))// serving cell info + MIB

// Subpacket IDs that appear inside the 0xB193 container.
enum LteML1SubpktId : uint8_t {
    LTE_ML1_Serving_Cell_Meas_Results = 0x19,
};

// ─────────────────────────────────────────────────────────────────────────────
class DiagLteMl1Parser {
public:
    using CellCallback = std::function<void(const LteCell &)>;

    DiagLteMl1Parser() = default;
    ~DiagLteMl1Parser() = default;

    void set_cell_callback(CellCallback cb) { cell_cb_ = std::move(cb); }
    void set_debug(bool on) { debug_ = on; }

    static std::vector<uint16_t> handled_log_codes();

    bool parse(const uint8_t *buf, size_t len);

    const LteCell &serving_cell() const { return serving_; }
    const std::vector<LteCell> &neighbors() const { return neighbors_; }

private:
    // Per-log-code handlers (operate on payload *after* the 12-byte LogRecord).
    bool parse_serving_meas_eval(const uint8_t *p, size_t plen);// 0xB17F
    bool parse_neighbor_meas(const uint8_t *p, size_t plen);    // 0xB180
    bool parse_scell_meas_resp(const uint8_t *p, size_t plen);  // 0xB193
    bool parse_serving_cell_info(const uint8_t *p, size_t plen);// 0xB197

    // 0xB193 subpkt 0x19 per-cell decoders (scat parity).
    bool scell_resp_cell_v36(int idx, const uint8_t *cell, size_t clen,
                             uint32_t earfcn, bool &serving_out);
    bool scell_resp_cell_v48(int idx, const uint8_t *cell, size_t clen,
                             uint32_t earfcn, bool &serving_out);

    // Cell emission helpers.
    void emit_cell(const LteCell &c);
    void add_or_update_neighbor(const LteCell &c);

    // One-time fail-closed diagnostic for an unknown (code, version) pair.
    // Dumps hex to logcat ONCE per pair so the stream is not spammed; the user
    // sends us that hex and we add the exact layout instead of guessing.
    void report_unsupported(uint16_t code, uint8_t ver,
                            const uint8_t *p, size_t plen);

    LteCell serving_{};
    std::vector<LteCell> neighbors_;
    CellCallback cell_cb_;
    bool debug_ = false;

    // Authoritative serving EARFCN/PCI from 0xB197 (Serving Cell Info). On this
    // device 0xB17F (Serving Meas) reports a stuck/wrong PCI, so we override
    // 0xB17F's PCI with this one before attaching its RSRP — otherwise the
    // serving RSRP never merges onto the real (RRC) serving cell. 0xFFFF = none.
    uint32_t srv_info_earfcn_ = 0;
    uint16_t srv_info_pci_ = 0xFFFF;
};

#endif// DIAG_LTE_ML1_PARSER_H
