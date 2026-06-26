#pragma once
#ifndef DIAG_GSM_LOG_PARSER_H
#define DIAG_GSM_LOG_PARSER_H

#include "protocol/diag_common.h"
#include "gsm_cell_metrics.h"
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// GSM log codes  (LOG_GSM_BASE_C = 0x5000)
// Mirrors SCAT diaggsmlogparser.py handle_*
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_GSM_L1_FCCH_ACQUISITION_C ((uint16_t) (0x5065))//GSM L1 FCCH Acquisition
#define LOG_GSM_L1_SCH_ACQUISITION_C ((uint16_t) (0x5066))
#define LOG_GSM_L1_BURST_METRICS_C ((uint16_t) (0x506C))
#define LOG_GSM_L1_NEW_BURST_METRICS_C ((uint16_t) (0x506A))
#define LOG_GSM_L1_SURROUND_CELL_BA_LIST_C ((uint16_t) (0x5071))// BA list
#define LOG_GSM_L1_SERV_AUX_MEAS ((uint16_t) (0x507A))
#define LOG_GSM_L1_NEIG_AUX_MEAS ((uint16_t) (0x507B))
#define LOG_GSM_CELL_INFO ((uint16_t) (0x5134))
#define LOG_GSM_GSM_RR ((uint16_t) (0x512F))
#define LOG_GPRS_OTA ((uint16_t) (0x5230))

#define LOG_GSM_DSDS_FCCH ((uint16_t) (0x5A65))
#define LOG_GSM_DSDS_SCH ((uint16_t) (0x5A66))
#define LOG_GSM_DSDS_L1_BURST_METRIC ((uint16_t) (0x5A6C))
#define LOG_GSM_DSDS_L1_SURROUND_CELL_BA ((uint16_t) (0x5A71))
#define LOG_GSM_DSDS_L1_SERV_AUX_MEAS ((uint16_t) (0x5A7A))
#define LOG_GSM_DSDS_L1_NEIG_AUX_MEAS ((uint16_t) (0x5A7B))
#define LOG_GSM_DSDS_CELL_INFO ((uint16_t) (0x5B34))
#define LOG_GSM_DSDS_RR ((uint16_t) (0x5B2F))


static inline uint16_t rd_u16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

static inline int16_t rd_s16(const uint8_t *p) {
    return static_cast<int16_t>(rd_u16(p));
}

static inline uint32_t rd_u32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// ─────────────────────────────────────────────────────────────────────────────
// Packed GSM structures
// ─────────────────────────────────────────────────────────────────────────────
//#pragma pack(push, 1)
//
//// 0x506C – RR cell information (serving cell)
//struct GsmRrCellInfo {
//    uint8_t  cell_id_high;
//    uint8_t  cell_id_low;
//    uint8_t  lac_high;
//    uint8_t  lac_low;
//    uint16_t arfcn;         // absolute RF channel number
//    uint8_t  bsic;          // base station identity code
//    uint8_t  rxlev_access_min;
//    uint8_t  ms_txpwr_max_cch;
//    int8_t   rxlev_sub;     // dBm + 110
//    int8_t   rxlev_full;
//    uint8_t  rxqual_sub;
//    uint8_t  rxqual_full;
//    uint8_t  ta;            // timing advance
//};
//
//// 0x5075 – neighbor cell list entry
//struct GsmNeighborCellEntry {
//    uint16_t arfcn;
//    uint8_t  bsic;
//    int8_t   rxlev;         // raw, subtract 110 for dBm
//};
//
//// 0x507B – surround measurement entry
//struct GsmSurroundMeasEntry {
//    uint16_t arfcn;
//    uint8_t  bsic;
//    int8_t   rxlev;
//    uint8_t  valid;
//};
//
//struct GsmSurroundMeas {
//    uint8_t             num_neighbors;
//    // followed by num_neighbors × GsmSurroundMeasEntry
//};
//
//// 0x507A – BA list
//struct GsmBaListHeader {
//    uint8_t  ba_ind;
//    uint8_t  num_cells;
//    // followed by num_cells × GsmNeighborCellEntry
//};
//
//#pragma pack(pop)
#pragma pack(push, 1)

// DSDS prefix: only if you want a tiny wrapper for the first byte
struct QcDiagDsdsPrefix {
    uint8_t radio_id;
};

// 0x5065: GSM L1 FCCH Acquisition
struct QcDiagGsmL1Fcch {
    uint16_t arfcn_band;
    uint16_t tone_id;
    uint16_t msw;
    uint16_t lsw;
    int16_t coarse_freq_offset;
    int16_t fine_freq_offset;
    int16_t afc_freq;
    uint16_t snr;
};

// 0x5066: GSM L1 SCH Acquisition
struct QcDiagGsmL1Sch {
    uint16_t arfcn_band;
    uint16_t tone_id;
    uint16_t crc_pass;
    uint16_t dsp_rx;
    uint16_t bad_frame;
    uint16_t decoded_data_len;
    uint32_t decoded_data;
    uint16_t msw;
    uint16_t lsw;
    uint16_t peak_corr_energy;
    uint16_t freq_offset;
};

// 0x506A: GSM L1 New Burst Metric, version 4 record
struct QcDiagGsmL1NewBurstMetricV4 {
    uint32_t sfn;
    uint16_t arfcn_band;
    uint32_t rssi;
    int16_t rxpwr;
    int16_t dcoff_i;
    int16_t dcoff_q;
    int16_t freq_offset;
    int16_t time_offset;
    int16_t snr_est;
    int8_t gain_state;
    int8_t aci;
    uint32_t q16;
    uint8_t aqpsk;
    uint8_t timeslot;
    uint16_t jdet_reading_divrx;
    uint32_t wb_power;
    uint8_t ll_hl_state;
};

// 0x506C: GSM L1 Burst Metrics
struct QcDiagGsmL1BurstMetric {
    uint32_t sfn;
    uint16_t arfcn_band;
    uint32_t rssi;
    int16_t rxpwr;
    int16_t dcoff_i;
    int16_t dcoff_q;
    int16_t freq_offset;
    int16_t time_offset;
    int16_t snr_est;
    int8_t gain_state;
};

// 0x5071: GSM Surround Cell BA list entry
struct QcDiagGsmL1SurroundCellBa {
    uint16_t arfcn_band;
    int16_t rxpwr;
    uint8_t bsic_valid;
    uint8_t bsic;
    uint32_t fn_offset;
    uint16_t time_offset;
};

// 0x507A: GSM serving cell auxiliary measurements
struct QcDiagGsmL1ServAuxMeas {
    int16_t rxpwr;
    uint8_t snr_is_bad;
};

// 0x507B: GSM neighbor auxiliary measurements
struct QcDiagGsmL1NeigAuxMeas {
    uint16_t arfcn_band;
    int16_t rxpwr;
};

// 0x5134: GSM RR Cell Information
struct QcDiagGsmRrCellInfo {
    uint16_t arfcn_band;
    uint8_t bcc;
    uint8_t ncc;
    uint16_t cid;
    uint8_t lai[5];
    uint8_t priority;
    uint8_t ncc_permitted;
};

// 0x512F: GSM RR Signaling Message
struct QcDiagGsmRrSignalingMessage {
    uint8_t channel_type_dir;
    uint8_t message_type;
    uint8_t message_len;
};

// 0x5230: GPRS SM/GMM OTA Signaling Message
struct QcDiagGsmGprsOta {
    uint8_t msg_dir;
    uint8_t message_type;
    uint16_t message_len;
};

// 0x5226: GPRS MAC Signaling Message
struct QcDiagGsmGprsMac {
    uint8_t channel_type_dir;
    uint8_t message_type;
    uint8_t message_len;
};

#pragma pack(pop)

#pragma pack(push, 1)

struct GsmFcchPayload {
    uint16_t arfcn_band_le;
};

struct GsmDsdsPrefix {
    uint8_t radio_id;
};

struct GsmL1BurstMetric {
    uint32_t fn;
    uint16_t arfcn_band;
    int16_t rssi;
    int16_t rxpwr;
    int16_t dcoff_i;
    int16_t dcoff_q;
    int16_t freq_offset;
    int16_t time_offset;
    int16_t snr_est;
    int8_t gain_state;
};

struct GsmL1NewBurstMetric {
    uint32_t fn;
    uint16_t arfcn_band;
    int16_t rssi;
    int16_t rxpwr;
    int16_t dcoff_i;
    int16_t dcoff_q;
    int16_t freq_offset;
    int16_t time_offset;
    int16_t snr_est;
    int8_t gain_state;
    int8_t aci;
    uint32_t q16;
    uint8_t aqpsk;
    uint8_t timeslot;
    uint16_t jdet_reading_divrx;
    uint32_t wb_power;
    uint8_t ll_hl_state;
};

struct GsmSurroundCellBa {
    uint16_t arfcn_band;
    int16_t rxpwr;
    uint16_t bsic;
    uint32_t fn_offset;
    uint16_t time_offset;
};

struct GsmServAuxMeas {
    int16_t rxpwr;
    uint8_t snr_is_bad;
};

struct GsmNeigAuxMeas {
    uint16_t arfcn_band;
    int16_t rxpwr;
};

struct GsmCellInfo {
    uint16_t arfcn_band;
    uint8_t bcc;
    uint8_t ncc;
    uint16_t cid;
    uint8_t lai[5];
    uint8_t priority;
    uint8_t ncc_permitted;
};

struct GsmRrHeader {
    uint8_t chan_type_dir;
    uint8_t msg_type;
    uint8_t msg_len;
};

struct GprsOtaHeader {
    uint8_t msg_dir;
    uint8_t msg_type;
    uint16_t msg_len;
};

#pragma pack(pop)


// ─────────────────────────────────────────────────────────────────────────────
// Parser class
// ─────────────────────────────────────────────────────────────────────────────
class DiagGsmLogParser {
public:
    using LogCallback = std::function<void(const GsmCell &)>;

    DiagGsmLogParser() = default;
    ~DiagGsmLogParser() = default;

    void set_cell_callback(LogCallback cb) { cell_cb_ = std::move(cb); }
    void set_surround_arfcn_hint(uint16_t a) {
        surround_arfcn_hint_ = a;
        surround_hint_fresh_ = true;
    }
    void set_debug(bool on) { debug_ = on; }

    static std::vector<uint16_t> handled_log_codes();
    bool parse(const uint8_t *buf, size_t len);

    const GsmCell &serving_cell() const { return serving_; }
    const std::vector<GsmCell> &neighbors() const { return neighbors_; }

private:
    //    bool parse_rr_cell_information(const uint8_t* payload, size_t plen);
    //    bool parse_neighbor_cell_info (const uint8_t* payload, size_t plen);
    //    bool parse_ba_list            (const uint8_t* payload, size_t plen);
    //    bool parse_surround_meas      (const uint8_t* payload, size_t plen);

    bool parse_gsm_fcch(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_fcch(const uint8_t *payload, size_t plen);

    bool parse_gsm_sch(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_sch(const uint8_t *payload, size_t plen);

    bool parse_gsm_l1_burst_metric(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_l1_burst_metric(const uint8_t *payload, size_t plen);

    bool parse_gsm_l1_new_burst_metric(const uint8_t *payload, size_t plen);

    bool parse_gsm_l1_surround_cell_ba(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_l1_surround_cell_ba(const uint8_t *payload, size_t plen);

    bool parse_gsm_l1_serv_aux_meas(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_l1_serv_aux_meas(const uint8_t *payload, size_t plen);

    bool parse_gsm_l1_neig_aux_meas(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_l1_neig_aux_meas(const uint8_t *payload, size_t plen);

    bool parse_gsm_cell_info(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_cell_info(const uint8_t *payload, size_t plen);

    bool parse_gsm_rr(const uint8_t *payload, size_t plen);
    bool parse_gsm_dsds_rr(const uint8_t *payload, size_t plen);

    bool parse_gprs_ota(const uint8_t *payload, size_t plen);

    GsmCell serving_{};
    std::vector<GsmCell> neighbors_;
    LogCallback cell_cb_;
    uint16_t surround_arfcn_hint_ = 0;// ARFCN from GPRS_SURROUND_SEARCH_START
    bool surround_hint_fresh_ = false;// consume-once guard
    bool debug_ = false;

    // Cell Selection / reselection parameters for the serving cell, captured
    // from SI-3/SI-4 BCCH — used to compute C1/C2 once we have a measured RxLev.
    gsm_metrics::CellSelParams serving_sel_{};
    std::map<uint16_t, int16_t> arfcn_rxlev_;                 // ARFCN -> last measured RxLev (for neighbour C1/C2)
    std::map<uint16_t, gsm_metrics::CellSelParams> arfcn_sel_;// ARFCN -> its SI-3 cell-selection params
    void recompute_serving_c1c2();
    bool neighbor_c1c2(uint16_t arfcn, int16_t rxlev, int16_t &c1, int16_t &c2);
};

#endif// DIAG_GSM_LOG_PARSER_H
