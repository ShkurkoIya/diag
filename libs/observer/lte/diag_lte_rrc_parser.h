#pragma once
#ifndef DIAG_LTE_RRC_PARSER_H
#define DIAG_LTE_RRC_PARSER_H

#include "diag_common.h"
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// LTE RRC log codes
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_LTE_RRC_OTA_C ((uint16_t) (0xB0C0))           // RRC OTA (MIB/SIB/...)
#define LOG_LTE_RRC_MIB_C ((uint16_t) (0xB0C1))           // dedicated MIB log
#define LOG_LTE_RRC_SERV_CELL_INFO_C ((uint16_t) (0xB0C2))// Serving Cell Info ★

// ─────────────────────────────────────────────────────────────────────────────
// LteCellIdentity — authoritative cell identity from RRC layer
//
// Source of truth for serving cell identification. Owned by DiagLteRrcParser.
// DiagLteNasParser fills in MCC/MNC/TAC as fallback (from NAS Attach Accept).
//
// Field sources:
//   - 0xB0C2 (Serving Cell Info v2/v3): PCI, EARFCN(DL+UL), CID, TAC, MCC, MNC,
//     Band, BW, allowed_access ★ primary
//   - 0xB0C1 (MIB): SFN, PHICH duration/resource (supplemental)
//   - 0xB0C0 (RRC OTA): rarely useful on SM8550 (DCI firmware filter blocks SIBs)
// ─────────────────────────────────────────────────────────────────────────────
struct LteCellIdentity {
    // From 0xB0C2 — primary
    uint32_t cell_id = 0;// 28-bit ECI
    uint16_t tac = 0;
    uint16_t mcc = 0;           // decimal (250, 262, ...)
    uint16_t mnc = 0;           // decimal
    uint8_t mnc_digit_count = 0;// 2 or 3
    uint32_t earfcn = 0;        // DL EARFCN
    uint32_t ul_earfcn = 0;
    uint16_t pci = 0;
    uint32_t band = 0;// 3GPP band
    uint8_t dl_bw = 0;
    uint8_t ul_bw = 0;
    uint8_t allowed_access = 0;

    // From 0xB0C1 — supplemental
    uint16_t sfn = 0;
    uint8_t phich_duration = 0;
    uint8_t phich_resource = 0;

    bool valid = false;
};

// A single rsrp/rsrq measurement from a UL-DCCH MeasurementReport. Serving
// (PCell) carries the OTA packet's earfcn/pci; neighbors carry their PCI only
// (earfcn=0 — matched by PCI in the merge layer). 0 = field absent.
struct LteMeasReport {
    uint32_t earfcn = 0;
    int pci = -1;
    int rsrp = 0;
    int rsrq = 0;
    bool is_serving = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// DiagLteRrcParser — handles RRC-layer log codes
//
// Code      | Function                       | Yields
// ──────────┼────────────────────────────────┼─────────────────────────────────
// 0xB0C0    | parse_rrc_ota                  | MIB/SIB/DCCH... (mostly MIB only on SM8550)
// 0xB0C1    | parse_rrc_mib                  | SFN, PHICH, BW
// 0xB0C2    | parse_rrc_serving_cell_info ★  | full identity (MCC/MNC/CID/TAC/...)
// ─────────────────────────────────────────────────────────────────────────────
class DiagLteRrcParser {
public:
    using IdentityCallback = std::function<void(const LteCellIdentity &)>;

    DiagLteRrcParser() = default;

    void set_identity_callback(IdentityCallback cb) { id_cb_ = std::move(cb); }
    void set_debug(bool on) { debug_ = on; }

    static std::vector<uint16_t> handled_log_codes();
    bool parse(const uint8_t *buf, size_t len);

    const LteCellIdentity &identity() const { return id_; }

    /** SIB1-decoded cells (typically foreign-PLMN cells discovered during
     *  PLMN search). Drained by QualcommLogParser::merge_lte_identity()
     *  after each parse cycle. */
    std::vector<LteCell> &sib1_cells() { return sib1_cells_; }
    const std::vector<LteCell> &sib1_cells() const { return sib1_cells_; }

    /** Accumulated 0xB0C2 serving-cell identities (one per distinct
     *  earfcn+pci the modem camped on during the sweep). identity() keeps
     *  only the LATEST; this list keeps them ALL so merge_lte_identity()
     *  can stamp MCC/MNC/CID/TAC onto every matching ML1 measurement. */
    std::vector<LteCell> &serv_cells() { return serv_cells_; }
    const std::vector<LteCell> &serv_cells() const { return serv_cells_; }

    /** UL-DCCH MeasurementReport measurements (rsrp/rsrq by PCI). Drained by
     *  QualcommLogParser::merge_lte_identity() to fill missing signal values. */
    std::vector<LteMeasReport> &meas_reports() { return meas_reports_; }
    const std::vector<LteMeasReport> &meas_reports() const { return meas_reports_; }

private:
    bool parse_rrc_ota(const uint8_t *p, size_t plen);              // 0xB0C0
    bool parse_rrc_mib(const uint8_t *p, size_t plen);              // 0xB0C1
    bool parse_rrc_sib1(const uint8_t *p, size_t plen,              // 0xB0C0 pdu_num=3
                        uint32_t earfcn, uint16_t pci);             //   foreign-cell SIB1 decode
    bool parse_rrc_ul_dcch(const uint8_t *p, size_t plen,           // 0xB0C0 pdu_num=11
                           uint32_t earfcn, uint16_t pci);          //   MeasurementReport rsrp/rsrq
    bool parse_rrc_serving_cell_info(const uint8_t *p, size_t plen);// 0xB0C2

    // Upsert the current id_ (latest B0C2 decode) into serv_cells_ keyed by
    // (earfcn,pci) so every camped cell's identity is retained for the merge.
    void stash_serving_cell();

    LteCellIdentity id_;
    IdentityCallback id_cb_;
    std::vector<LteCell> sib1_cells_;                 // foreign-PLMN cells from SIB1
    std::vector<LteCell> serv_cells_;                 // accumulated B0C2 serving identities
    std::vector<LteMeasReport> meas_reports_;         // rsrp/rsrq from UL-DCCH MeasurementReport
    std::unordered_map<int, uint32_t> neigh_freq_map_;// PCI → EARFCN from SIB4/SIB5
    bool debug_ = true;
};

#endif// DIAG_LTE_RRC_PARSER_H
