#pragma once
#ifndef DIAG_WCDMA_LOG_PARSER_H
#define DIAG_WCDMA_LOG_PARSER_H

#include "protocol/diag_common.h"

// ─────────────────────────────────────────────────────────────────────────────
// WCDMA log codes  (LOG_WCDMA_BASE_C = 0x4000)
// Mirrors SCAT diagwcdmalogparser.py
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_WCDMA_SEARCH_CELL_RESELECTION_RANK_C ((uint16_t) (0x4005))
#define LOG_WCDMA_CELL_ID_C ((uint16_t) (0x4027))
#define LOG_WCDMA_NEIGHBOR_SET_C ((uint16_t) (0x4111))     // primary neighbor-set log
#define LOG_WCDMA_SERVING_CELL_INFO_C ((uint16_t) (0x4127))// serving cell signal
#define LOG_WCDMA_RRC_OTA_C ((uint16_t) (0x412F))          // RRC signaling (OTA)
#define LOG_WCDMA_FREQ_SCAN_C ((uint16_t) (0x41AC))
#define LOG_WCDMA_CELL_RESEL_C ((uint16_t) (0x41B0))
#define LOG_WCDMA_RXD_INFO_C ((uint16_t) (0x4801))
#define LOG_WCDMA_DRX_CTRL_INFO_C ((uint16_t) (0x4802))

// ─────────────────────────────────────────────────────────────────────────────
// Packed WCDMA structures
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)

// ---- 0x4127 – Serving Cell Info ----
struct WcdmaServingCellInfo {
    uint16_t uarfcn_dl;
    uint16_t psc;
    int16_t rscp;// raw; divide by 4096 for dBm on some fw, or already dBm
    int16_t ecno;// raw; divide by 512 for dB
    uint32_t cell_id;
    uint8_t plmn[3];
    uint16_t lac;
    uint8_t rac;
};

// ---- 0x4111 – Neighbor Set ----
// Per-cell entry inside per-frequency info
struct WcdmaNeighborCellEntry {
    uint16_t psc;
    int16_t rscp;// dBm
    int16_t ecno;// dB  (Ec/Io, already in 0.5 dB units on most fw)
    uint8_t rank;
    uint8_t flags;
};

struct WcdmaFreqInfo {
    uint16_t uarfcn;
    uint8_t num_cells;
    // followed by num_cells × WcdmaNeighborCellEntry
    // NOTE: layout may have extra padding on some fw versions
};

struct WcdmaNeighborSetHeader {
    uint8_t version;
    uint16_t num_freq;
    // followed by num_freq × (WcdmaFreqInfo + cells)
};

// ---- 0x4005 – Cell reselection rank ----
struct WcdmaReselRankEntry {
    uint16_t uarfcn;
    uint16_t psc;
    int32_t rank;
    int16_t rscp;
    int16_t ecno;
};

struct WcdmaReselRankHeader {
    uint8_t num_cells;
    // followed by num_cells × WcdmaReselRankEntry
};

// ---- 0x4027 – Cell ID (serving cell ID/PLMN) ----
struct WcdmaCellIdInfo {
    uint32_t cell_id;
    uint16_t uarfcn;
    uint16_t psc;
    uint16_t lac;
    uint8_t plmn[3];
    uint8_t rac;
};

#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
class DiagWcdmaLogParser {
public:
    using CellCallback = std::function<void(const WcdmaCell &)>;

    DiagWcdmaLogParser() = default;
    ~DiagWcdmaLogParser() = default;

    void set_cell_callback(CellCallback cb) { cell_cb_ = std::move(cb); }

    static std::vector<uint16_t> handled_log_codes();

    bool parse(const uint8_t *buf, size_t len);

    const WcdmaCell &serving_cell() const { return serving_; }
    const std::vector<WcdmaCell> &neighbors() const { return neighbors_; }

private:
    bool parse_neighbor_set(const uint8_t *payload, size_t plen);
    bool parse_serving_cell(const uint8_t *payload, size_t plen);
    bool parse_resel_rank(const uint8_t *payload, size_t plen);
    bool parse_cell_id(const uint8_t *payload, size_t plen);
    bool parse_rrc_ota(const uint8_t *payload, size_t plen);// 0x412F UMTS RRC

    // Convert raw RSCP/EcNo from modem representation to dBm / dB
    static int16_t raw_rscp_to_dbm(int16_t raw);
    static int16_t raw_ecno_to_db(int16_t raw);

    WcdmaCell serving_{};
    std::vector<WcdmaCell> neighbors_;
    CellCallback cell_cb_;
};

#endif// DIAG_WCDMA_LOG_PARSER_H
