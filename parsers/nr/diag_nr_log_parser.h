#pragma once
#ifndef DIAG_NR_LOG_PARSER_H
#define DIAG_NR_LOG_PARSER_H

#include "diag_common.h"

// ─────────────────────────────────────────────────────────────────────────────
// NR5G log codes (LOG_NR_BASE_C ≈ 0xB800)
// Based on SCAT diagnrlogparser.py and Qualcomm ICD
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_NR5G_ML1_MEAS_DB_UPDATE_C         ((uint16_t)(0xB97F))  // Searcher meas DB update
#define LOG_NR5G_ML1_SERVING_CELL_MEAS_C      ((uint16_t)(0xB992))  // Serving cell meas
#define LOG_NR5G_RRC_OTA_C                    ((uint16_t)(0xB821))  // RRC OTA message
#define LOG_NR5G_NAS_SM_OTA_C                 ((uint16_t)(0xB800))  // NAS 5GS OTA
#define LOG_NR5G_ML1_SEARCHER_MEAS_C          ((uint16_t)(0xB975))  // Searcher meas
#define LOG_NR5G_MAC_RACH_ATTEMPT_C           ((uint16_t)(0xB887))  // RACH attempt

// ─────────────────────────────────────────────────────────────────────────────
// NR5G subpacket container header
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)

struct NrLogPktHeader {
    uint8_t  minor_ver;
    uint8_t  major_ver;
    uint8_t  num_subpkts;
    uint8_t  reserved;
};

struct NrLogSubPkt {
    uint8_t  id;
    uint8_t  ver;
    uint16_t size;
};

#pragma pack(pop)

// NrCell struct is defined in diag_common.h

// ─────────────────────────────────────────────────────────────────────────────
class DiagNrLogParser {
public:
    using CellCallback = std::function<void(const NrCell&)>;

    DiagNrLogParser() = default;
    ~DiagNrLogParser() = default;

    void set_cell_callback(CellCallback cb) { cell_cb_ = std::move(cb); }

    static std::vector<uint16_t> handled_log_codes();

    bool parse(const uint8_t* buf, size_t len);

    const NrCell&              serving_cell() const { return serving_; }
    const std::vector<NrCell>& neighbors()    const { return neighbors_; }

private:
    bool parse_meas_db_update(const uint8_t* p, size_t plen);
    bool parse_serving_cell_meas(const uint8_t* p, size_t plen);

    // Bitfield helpers (same as LTE)
    static inline uint32_t read_u32(const uint8_t* p) {
        uint32_t v; memcpy(&v, p, 4); return v;
    }
    static inline uint16_t read_u16(const uint8_t* p) {
        uint16_t v; memcpy(&v, p, 2); return v;
    }
    static inline uint32_t bits(uint32_t word, int lsb, int nbits) {
        return (word >> lsb) & ((1u << nbits) - 1u);
    }

    // NR RSRP conversion: raw * 0.0625 - 180.0 (same scale as LTE)
    static inline int16_t convert_nr_rsrp(uint32_t raw) {
        double dbm = static_cast<double>(raw) * 0.0625 - 180.0;
        if (dbm < -180.0) dbm = -180.0;
        if (dbm > -30.0)  dbm = -30.0;
        return static_cast<int16_t>(dbm - 0.5);
    }
    static inline int16_t convert_nr_rsrq(uint32_t raw) {
        double db = static_cast<double>(raw) * 0.0625 - 30.0;
        return static_cast<int16_t>(db - 0.5);
    }
    static inline int16_t convert_nr_sinr(uint32_t raw) {
        double db = static_cast<double>(raw) * 0.0625 - 20.0;
        return static_cast<int16_t>(db < 0 ? db - 0.5 : db + 0.5);
    }

    void add_or_update_neighbor(const NrCell& c);

    NrCell               serving_  {};
    std::vector<NrCell>  neighbors_;
    CellCallback         cell_cb_;
};

#endif // DIAG_NR_LOG_PARSER_H
