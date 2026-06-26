#include "diag_gsm_log_parser.h"
#include "journal.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

using namespace diag_util;

// Defined further down — declared here so parse_gsm_rr (System Information)
// can reuse the same LAI decoder as 0x5134.
static inline void unpack_lai(const uint8_t lai[5], uint16_t &mcc, uint16_t &mnc, uint16_t &lac);

std::vector<uint16_t> DiagGsmLogParser::handled_log_codes() {
    return {
            LOG_GSM_CELL_INFO,
            LOG_GSM_DSDS_CELL_INFO,
            LOG_GSM_DSDS_L1_SERV_AUX_MEAS,
            LOG_GSM_DSDS_L1_NEIG_AUX_MEAS,
            LOG_GSM_L1_SERV_AUX_MEAS,
            LOG_GSM_L1_NEIG_AUX_MEAS,
            LOG_GSM_L1_SURROUND_CELL_BA_LIST_C,
            LOG_GSM_DSDS_L1_SURROUND_CELL_BA,
            LOG_GSM_L1_FCCH_ACQUISITION_C,
            LOG_GSM_L1_SCH_ACQUISITION_C,
            LOG_GSM_L1_BURST_METRICS_C,
            LOG_GSM_L1_NEW_BURST_METRICS_C,
            LOG_GSM_GSM_RR,
            0x5075,
            0x5A75,
            LOG_GSM_DSDS_FCCH,
            LOG_GSM_DSDS_SCH,
            LOG_GSM_DSDS_L1_BURST_METRIC,
            LOG_GSM_DSDS_RR,
            LOG_GPRS_OTA,
            0xB1E1};
}

bool DiagGsmLogParser::parse(const uint8_t *buf, size_t len) {
    if (len < sizeof(LogRecord)) return false;
    const auto *hdr = reinterpret_cast<const LogRecord *>(buf);
    const uint8_t *payload = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);

    if (debug_) {
        DIAG_LOGD("GSM: code=0x%04X plen=%zu", hdr->code, plen);
        hex_dump("GSM", payload, plen, 48);
    }

    switch (hdr->code) {
        case LOG_GSM_L1_FCCH_ACQUISITION_C:
            return parse_gsm_fcch(payload, plen);

        case LOG_GSM_L1_SCH_ACQUISITION_C:
            return parse_gsm_sch(payload, plen);

        case LOG_GSM_L1_BURST_METRICS_C:
            return parse_gsm_l1_burst_metric(payload, plen);

        case LOG_GSM_L1_NEW_BURST_METRICS_C:
            return parse_gsm_l1_new_burst_metric(payload, plen);

        case LOG_GSM_L1_SURROUND_CELL_BA_LIST_C:
            return parse_gsm_l1_surround_cell_ba(payload, plen);

        case LOG_GSM_L1_SERV_AUX_MEAS:
            return parse_gsm_l1_serv_aux_meas(payload, plen);

        case LOG_GSM_L1_NEIG_AUX_MEAS:
            return parse_gsm_l1_neig_aux_meas(payload, plen);

        case LOG_GSM_CELL_INFO:
            return parse_gsm_cell_info(payload, plen);

        case LOG_GSM_GSM_RR:
            return parse_gsm_rr(payload, plen);

        case LOG_GPRS_OTA:
            return parse_gprs_ota(payload, plen);

        case LOG_GSM_DSDS_FCCH:
            return parse_gsm_dsds_fcch(payload, plen);

        case LOG_GSM_DSDS_SCH:
            return parse_gsm_dsds_sch(payload, plen);

        case LOG_GSM_DSDS_L1_BURST_METRIC:
            return parse_gsm_dsds_l1_burst_metric(payload, plen);

        case LOG_GSM_DSDS_L1_SURROUND_CELL_BA:
            return parse_gsm_dsds_l1_surround_cell_ba(payload, plen);

        case LOG_GSM_DSDS_L1_SERV_AUX_MEAS:
            return parse_gsm_dsds_l1_serv_aux_meas(payload, plen);

        case LOG_GSM_DSDS_L1_NEIG_AUX_MEAS:
            return parse_gsm_dsds_l1_neig_aux_meas(payload, plen);

        case LOG_GSM_DSDS_CELL_INFO:
            return parse_gsm_dsds_cell_info(payload, plen);

        case LOG_GSM_DSDS_RR:
            return parse_gsm_dsds_rr(payload, plen);

        default:
            return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 0x506C – RR Cell Information (serving cell)
//
// The payload on modern Qualcomm (SM8550) is 96 bytes — much larger than the
// basic 14-byte GsmRrCellInfo struct. There's likely a version/header prefix.
//
// STRATEGY: Try to find the ARFCN by scanning for a valid value (0-1023)
// at expected offsets. Known ARFCN range for GSM: 0-124, 128-251, 512-885,
// 955-1023 (depends on band).
//
// Fallback: dump hex so user can diagnose.
// ═════════════════════════════════════════════════════════════════════════════
//bool DiagGsmLogParser::parse_rr_cell_information(const uint8_t* p, size_t plen) {
//    if (plen < 14) {
//        DIAG_LOGW("GSM 0x506C: too short (%zu)", plen);
//        return false;
//    }
//
//    if (debug_) hex_dump("0x506C", p, plen, 48);
//
//    // ── Try multiple struct offsets ──────────────────────────────────────
//    // Offset 0: original layout (no version prefix)
//    // Offset 1: 1-byte version prefix
//    // Offset 2: 2-byte version prefix
//    // Offset 4: 4-byte version prefix
//
//    struct Candidate {
//        size_t   offset;
//        uint16_t arfcn;
//        uint8_t  bsic;
//        int8_t   rxlev;
//        uint16_t lac;
//        uint16_t cid;
//    };
//
//    Candidate best = {0, 0xFFFF, 0, 0, 0, 0};
//
//    for (size_t base : {0, 1, 2, 4, 8}) {
//        if (base + sizeof(GsmRrCellInfo) > plen) continue;
//        const auto* ci = reinterpret_cast<const GsmRrCellInfo*>(p + base);
//
//        // ARFCN must be in valid GSM range (0-1023)
//        if (ci->arfcn <= 1023 && ci->bsic <= 63) {
//            // Also check that rxlev gives reasonable dBm (-110 to -40)
//            int16_t rxlev_dbm = static_cast<int16_t>(ci->rxlev_sub) - 110;
//            if (rxlev_dbm >= -115 && rxlev_dbm <= -20) {
//                best.offset = base;
//                best.arfcn  = ci->arfcn;
//                best.bsic   = ci->bsic;
//                best.rxlev  = ci->rxlev_sub;
//                best.lac     = (static_cast<uint16_t>(ci->lac_high) << 8) | ci->lac_low;
//                best.cid     = (static_cast<uint16_t>(ci->cell_id_high) << 8) | ci->cell_id_low;
//                break; // found valid candidate
//            }
//        }
//    }
//
//    if (best.arfcn > 1023) {
//        // No valid offset found — dump for diagnosis
//        DIAG_LOGW("GSM 0x506C: could not find valid ARFCN in payload");
//        if (debug_) {
//            for (size_t i = 0; i + 1 < plen && i < 20; ++i) {
//                uint16_t maybe = rd16(p + i);
//                if (maybe <= 1023 && maybe > 0)
//                    DIAG_LOGD("  potential ARFCN=%u at byte offset %zu", maybe, i);
//            }
//        }
//        return false;
//    }
//
//    serving_.arfcn   = best.arfcn;
//    serving_.bsic    = best.bsic;
//    serving_.rxlev   = static_cast<int16_t>(best.rxlev) - 110;
//    serving_.lac     = best.lac;
//    serving_.cid     = best.cid;
//    serving_.serving = true;
//
//    DIAG_LOGD("GSM serving (offset=%zu): ARFCN=%u BSIC=%u RxLev=%d LAC=%u CID=%u",
//              best.offset, serving_.arfcn, serving_.bsic, serving_.rxlev,
//              serving_.lac, serving_.cid);
//    if (cell_cb_) cell_cb_(serving_);
//    return true;
//}
//
//// ═════════════════════════════════════════════════════════════════════════════
//// 0x5075 – Neighbor cell list
//// ═════════════════════════════════════════════════════════════════════════════
//bool DiagGsmLogParser::parse_neighbor_cell_info(const uint8_t* p, size_t plen) {
//    if (plen < 1) return false;
//    if (debug_) hex_dump("0x5075", p, plen, 32);
//
//    uint8_t num = p[0];
//    size_t entry_size = sizeof(GsmNeighborCellEntry); // 4 bytes
//    size_t required = 1 + static_cast<size_t>(num) * entry_size;
//
//    if (plen < required) {
//        // Try with 1-byte version prefix
//        if (plen >= 2 && p[1] < 32 && p[1] > 0) {
//            num = p[1];
//            required = 2 + num * entry_size;
//            if (plen >= required) {
//                const auto* entries = reinterpret_cast<const GsmNeighborCellEntry*>(p + 2);
//                neighbors_.clear();
//                for (uint8_t i = 0; i < num; ++i) {
//                    if (entries[i].arfcn > 1023) continue;
//                    GsmCell c{};
//                    c.arfcn = entries[i].arfcn;
//                    c.bsic  = entries[i].bsic;
//                    c.rxlev = static_cast<int16_t>(entries[i].rxlev) - 110;
//                    c.serving = false;
//                    neighbors_.push_back(c);
//                    if (cell_cb_) cell_cb_(c);
//                }
//                return !neighbors_.empty();
//            }
//        }
//        if (debug_) DIAG_LOGW("GSM 0x5075: short payload (need %zu have %zu)", required, plen);
//        num = static_cast<uint8_t>((plen - 1) / entry_size);
//    }
//
//    neighbors_.clear();
//    const auto* entries = reinterpret_cast<const GsmNeighborCellEntry*>(p + 1);
//    for (uint8_t i = 0; i < num; ++i) {
//        if (entries[i].arfcn > 1023) continue; // skip invalid
//        GsmCell c{};
//        c.arfcn = entries[i].arfcn;
//        c.bsic  = entries[i].bsic;
//        c.rxlev = static_cast<int16_t>(entries[i].rxlev) - 110;
//        c.serving = false;
//        neighbors_.push_back(c);
//        if (cell_cb_) cell_cb_(c);
//    }
//    return !neighbors_.empty();
//}
//
//// ═════════════════════════════════════════════════════════════════════════════
//// 0x507A – BA list
//// ═════════════════════════════════════════════════════════════════════════════
//bool DiagGsmLogParser::parse_ba_list(const uint8_t* p, size_t plen) {
//    if (debug_) hex_dump("0x507A", p, plen, 20);
//
//    if (plen < 2) return false;
//    // Try different interpretations
//    // Original: ba_ind(1) + num_cells(1) + entries
//    // Maybe: version(1) + ba_ind(1) + num_cells(1) + entries
//    // The "short payload" error on 3-byte payload means the struct is wrong
//
//    // With 3 bytes: could be ba_ind + num_cells + 1 arfcn_byte?
//    // Just log for now
//    DIAG_LOGD("GSM 0x507A: plen=%zu (limited BA list parsing)", plen);
//    return true;
//}
//
//// ═════════════════════════════════════════════════════════════════════════════
//// 0x507B – Surround cell measurements
//// ═════════════════════════════════════════════════════════════════════════════
//bool DiagGsmLogParser::parse_surround_meas(const uint8_t* p, size_t plen) {
//    if (debug_) hex_dump("0x507B", p, plen, 24);
//
//    if (plen < 2) return false;
//
//    // The "short payload" error: expecting sizeof(GsmSurroundMeas)=1 + N*5 bytes
//    // but getting 9 bytes. With N=1: need 1+5=6. With 9 bytes: 1 entry + 3 extra?
//    // Or maybe version prefix shifts things.
//
//    // Try original layout: num_neighbors at [0]
//    uint8_t num = p[0];
//    size_t off = 1;
//
//    // If num is unreasonable, try offset 1
//    if (num > 32 && plen >= 2) {
//        num = p[1];
//        off = 2;
//    }
//
//    if (num > 32) return false;
//
//    neighbors_.clear();
//    for (uint8_t i = 0; i < num; ++i) {
//        if (off + sizeof(GsmSurroundMeasEntry) > plen) break;
//        const auto* e = reinterpret_cast<const GsmSurroundMeasEntry*>(p + off);
//        off += sizeof(GsmSurroundMeasEntry);
//
//        if (!e->valid) continue;
//        if (e->arfcn > 1023) continue;
//
//        GsmCell c{};
//        c.arfcn = e->arfcn;
//        c.bsic  = e->bsic;
//        c.rxlev = static_cast<int16_t>(e->rxlev) - 110;
//        c.serving = false;
//        neighbors_.push_back(c);
//        if (cell_cb_) cell_cb_(c);
//    }
//    return !neighbors_.empty();
//}


bool DiagGsmLogParser::parse_gsm_fcch(const uint8_t *p, size_t plen) {
    if (plen < sizeof(GsmFcchPayload))
        return false;

    QcDiagGsmL1Fcch s{};
    s.arfcn_band = diag_util::rd16(p);
    s.tone_id = diag_util::rd16(p + 2);
    s.msw = diag_util::rd16(p + 4);
    s.lsw = diag_util::rd16(p + 6);
    s.coarse_freq_offset = diag_util::rd16(p + 8);
    s.fine_freq_offset = diag_util::rd16(p + 10);
    s.afc_freq = diag_util::rd16(p + 12);
    s.snr = diag_util::rd16(p + 14);

    uint16_t arfcn = s.arfcn_band & 0x0FFF;//first 12 bits
    uint16_t band = (s.arfcn_band >> 12) & 0x0F;

    DIAG_LOGD("GSM FCCH: arfcn_band=%u, tone_id=%u, msw=%u, lsw=%u,"
              "coarse_freq_offset=%d, fine_freq_offset=%d, afc_freq=%d, snr=%d,"
              "ARFCN=%u, BAND=%u",
              s.arfcn_band, s.tone_id, s.msw, s.lsw,
              s.coarse_freq_offset, s.fine_freq_offset, s.afc_freq, s.snr,
              arfcn, band);
    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_fcch(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;

    GsmDsdsPrefix dsds{};
    memcpy(&dsds, p, sizeof(dsds));

    DIAG_LOGD("DSDS radio_id=%u", dsds.radio_id);

    return parse_gsm_fcch(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// SCH
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_sch(const uint8_t *p, size_t plen) {

    QcDiagGsmL1Sch s;
    s.arfcn_band = diag_util::rd16(p);
    s.tone_id = diag_util::rd16(p + 2);
    s.crc_pass = diag_util::rd16(p + 4);
    s.dsp_rx = diag_util::rd16(p + 6);
    s.bad_frame = diag_util::rd16(p + 8);
    s.decoded_data_len = diag_util::rd16(p + 10);
    s.decoded_data = diag_util::rd32(p + 12);
    s.msw = diag_util::rd16(p + 16);
    s.lsw = diag_util::rd16(p + 18);
    s.peak_corr_energy = diag_util::rd16(p + 20);
    s.freq_offset = diag_util::rd16(p + 22);

    uint16_t arfcn = s.arfcn_band & 0x0FFF;//first 12 bits
    uint16_t band = (s.arfcn_band >> 12) & 0x0F;

    DIAG_LOGD("GSM SCH: arfcn_band=%u, tone_id=%u, msw=%u, lsw=%u,"
              "crc_pass=%d, dsp_rx=%d, bad_frame=%d, decoded_data_len=%d,"
              "decoded_data=%ul, peak_corr_energy=%u, freq_offset=%u"
              "ARFCN=%u, BAND=%u",
              s.arfcn_band, s.tone_id, s.msw, s.lsw,
              s.crc_pass, s.dsp_rx, s.bad_frame, s.decoded_data_len,
              s.decoded_data, s.peak_corr_energy, s.freq_offset,
              arfcn, band);
    return parse_gsm_fcch(p, plen);
}

bool DiagGsmLogParser::parse_gsm_dsds_sch(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;

    return parse_gsm_sch(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// L1 BURST METRIC
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_l1_burst_metric(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;

    uint8_t channel = p[0];
    (void) channel;

    QcDiagGsmL1BurstMetric s;
    s.sfn = diag_util::rd32(p + 1);
    s.arfcn_band = rd16(p + 5);
    s.rssi = rd32(p + 7);
    s.rxpwr = rd16(p + 11);
    s.dcoff_i = rd16(p + 13);
    s.dcoff_q = rd16(p + 15);
    s.freq_offset = rd16(p + 17);
    s.time_offset = rd16(p + 19);
    s.snr_est = rd16(p + 21);
    s.gain_state = p[23];

    uint16_t arfcn = s.arfcn_band & 0x0FFF;//first 12 bits
    uint16_t band = (s.arfcn_band >> 12) & 0x0F;
    float rxpwr_real;
    if (s.rxpwr != 0) {
        rxpwr_real = s.rxpwr * 0.0625;
        DIAG_LOGD("GSM BURST METRIC: sfn=%ul, arfcn_band=%u, rssi=%ul, rxpwr=%d,"
                  "dcoff_i=%d, dcoff_q=%d, freq_offset=%d, time_offset=%d,"
                  "snr_est=%d, gain_state=%u"
                  "ARFCN=%u, BAND=%u, rxpwr_real=%.2f",
                  s.sfn, s.arfcn_band, s.rssi, s.rxpwr,
                  s.dcoff_i, s.dcoff_q, s.freq_offset, s.time_offset,
                  s.snr_est, s.gain_state,
                  arfcn, band, rxpwr_real);
    } else {
        DIAG_LOGD("GSM BURST METRIC: sfn=%ul, arfcn_band=%u, rssi=%ul, rxpwr=%d,"
                  "dcoff_i=%d, dcoff_q=%d, freq_offset=%d, time_offset=%d,"
                  "snr_est=%d, gain_state=%u"
                  "ARFCN=%u, BAND=%u",
                  s.sfn, s.arfcn_band, s.rssi, s.rxpwr,
                  s.dcoff_i, s.dcoff_q, s.freq_offset, s.time_offset,
                  s.snr_est, s.gain_state,
                  arfcn, band);
    }

    // If this burst is on the serving cell's ARFCN, update its signal level.
    if (s.rxpwr != 0 && serving_.arfcn != 0 && arfcn == serving_.arfcn) {
        serving_.rxlev = static_cast<int16_t>(s.rxpwr * 0.0625);
        recompute_serving_c1c2();
        if (cell_cb_) cell_cb_(serving_);
    }
    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_l1_burst_metric(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;
    uint8_t radio_id = p[0];
    DIAG_LOGD("GSM DSDS BURST METRIC: radio_id_pkt=%u", radio_id);
    return parse_gsm_l1_burst_metric(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW BURST METRIC
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_l1_new_burst_metric(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;

    uint8_t pkt_version = p[0];
    if (pkt_version == 4) {
        uint8_t channel = p[1];
        (void) channel;
        QcDiagGsmL1NewBurstMetricV4 s;
        s.sfn = diag_util::rd32(p + 2);
        s.arfcn_band = rd16(p + 6);
        s.rssi = rd32(p + 8);
        s.rxpwr = rd16(p + 12);
        s.dcoff_i = rd16(p + 14);
        s.dcoff_q = rd16(p + 16);
        s.freq_offset = rd16(p + 18);
        s.time_offset = rd16(p + 20);
        s.snr_est = rd16(p + 22);
        s.gain_state = p[24];
        s.aci = p[25];
        s.q16 = rd32(p + 26);
        s.aqpsk = p[30];
        s.timeslot = p[31];
        s.jdet_reading_divrx = rd16(p + 32);
        s.wb_power = rd32(p + 34);
        s.ll_hl_state = p[38];

        uint16_t arfcn = s.arfcn_band & 0x0FFF;//first 12 bits
        uint16_t band = (s.arfcn_band >> 12) & 0x0F;
        float rxpwr_real;
        if (s.rxpwr != 0) {
            rxpwr_real = s.rxpwr * 0.0625;
            DIAG_LOGD("GSM BURST METRIC: sfn=%ul, arfcn_band=%u, rssi=%ul, rxpwr=%d,"
                      "dcoff_i=%d, dcoff_q=%d, freq_offset=%d, time_offset=%d,"
                      "snr_est=%d, gain_state=%u"
                      "ARFCN=%u, BAND=%u, rxpwr_real=%.2f",
                      s.sfn, s.arfcn_band, s.rssi, s.rxpwr,
                      s.dcoff_i, s.dcoff_q, s.freq_offset, s.time_offset,
                      s.snr_est, s.gain_state,
                      arfcn, band, rxpwr_real);
        } else {
            DIAG_LOGD("GSM BURST METRIC: sfn=%ul, arfcn_band=%u, rssi=%ul, rxpwr=%d,"
                      "dcoff_i=%d, dcoff_q=%d, freq_offset=%d, time_offset=%d,"
                      "snr_est=%d, gain_state=%u"
                      "ARFCN=%u, BAND=%u",
                      s.sfn, s.arfcn_band, s.rssi, s.rxpwr,
                      s.dcoff_i, s.dcoff_q, s.freq_offset, s.time_offset,
                      s.snr_est, s.gain_state,
                      arfcn, band);
        }
    } else {
        DIAG_LOGW("Unsupported GSM Serving Cell L1 New Burst Metric version: %u", pkt_version);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SURROUND CELL BA
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_l1_surround_cell_ba(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;
    uint8_t num_cells = p[0];
    DIAG_LOGD("GSM SURROUND CELL BA num_cells=%u", num_cells);
    size_t off = 1;
    neighbors_.clear();// rebuild the neighbor list from this BA snapshot
    for (int i = 0; i < num_cells; i++) {
        if (off + sizeof(QcDiagGsmL1SurroundCellBa) > plen) {
            DIAG_LOGW("GSM SURROUND CELL BA: truncated packet at cell %u", i);
            break;
        }

        const uint8_t *cell_pkt = p + off;

        // Read the WHOLE entry from the packet (previously `s` was left
        // uninitialised, so bsic/bsic_valid were garbage).
        QcDiagGsmL1SurroundCellBa s{};
        memcpy(&s, cell_pkt, sizeof(s));

        uint16_t arfcn = s.arfcn_band & 0x0FFF;
        uint16_t band = (s.arfcn_band >> 12);
        int16_t rxpwr = s.rxpwr;
        double rxpwr_real = static_cast<double>(rxpwr) * 0.0625;

        if (s.bsic_valid == 1) {
            DIAG_LOGD("GSM SURROUND CELL BA: Cell #%u: ARFCN=%u BC=%u BSIC=%u RxPwr=%.2f",
                      i, arfcn, band, s.bsic, rxpwr_real);
        } else {
            DIAG_LOGD("GSM SURROUND CELL BA: Cell #%u: ARFCN=%u BC=%u BSIC=N/A RxPwr=%.2f",
                      i, arfcn, band, rxpwr_real);
        }

        // Emit as a neighbor (identity unknown — surround BA gives only
        // ARFCN/BSIC/signal, not CID/LAC/PLMN).
        GsmCell n{};
        n.arfcn = arfcn;
        n.band = static_cast<uint8_t>(band);
        n.bsic = s.bsic_valid ? s.bsic : 0xFF;     // 0xFF = BSIC unknown (no SCH sync)
        n.rxlev = static_cast<int16_t>(rxpwr_real);// dBm
        n.serving = false;
        neighbors_.push_back(n);
        if (n.arfcn != 0 && n.rxlev != 0) arfcn_rxlev_[n.arfcn] = n.rxlev;
        if (n.arfcn != 0 && n.rxlev != 0) {
            int16_t _c1 = 0, _c2 = 0;
            if (neighbor_c1c2(n.arfcn, n.rxlev, _c1, _c2)) {
                n.c1 = _c1;
                n.c2 = _c2;
            }
        }
        if (cell_cb_) cell_cb_(n);

        off += sizeof(QcDiagGsmL1SurroundCellBa);
    }

    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_l1_surround_cell_ba(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;
    uint8_t radio_id = p[0];
    DIAG_LOGD("GSM DSDS SURROUND CELL BA: radio_id_pkt=%u", radio_id);
    return parse_gsm_l1_surround_cell_ba(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// SERV AUX
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_l1_serv_aux_meas(const uint8_t *p, size_t plen) {
    if (plen < sizeof(GsmServAuxMeas)) {
        DIAG_LOGW("GSM SERV AUX MEAS ERROR plen<sizeof(GsmServAuxMeas)");
        return false;
    }

    GsmServAuxMeas m{};
    memcpy(&m, p, sizeof(m));

    double rxpwr_real = static_cast<double>(m.rxpwr) * 0.0625;

    DIAG_LOGD("GSM SERV AUX MEAS: ServingAux RxPwr=%.2f, snr_is_bad=%u", rxpwr_real, m.snr_is_bad);

    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_l1_serv_aux_meas(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;
    uint8_t radio_id = p[0];
    DIAG_LOGD("GSM DSDS SERV AUX MEAS: radio_id_pkt=%u", radio_id);
    return parse_gsm_l1_serv_aux_meas(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// NEIGH AUX
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_l1_neig_aux_meas(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;

    uint8_t num_cells = p[0];

    size_t off = 1;

    for (uint8_t i = 0; i < num_cells; ++i) {
        if (off + sizeof(GsmNeigAuxMeas) > plen) {
            DIAG_LOGW("GSM NEIG AUX MEAS: truncated packet at cell %u", i);
            break;
        }

        GsmNeigAuxMeas m{};
        memcpy(&m, p + off, sizeof(m));

        uint16_t arfcn = m.arfcn_band & 0x0FFF;
        uint16_t band = (m.arfcn_band >> 12);

        double rxpwr_real = static_cast<double>(m.rxpwr) * 0.0625;

        DIAG_LOGD("GSM NEIG AUX MEAS: Cell№%u, n_arfcn = %u, n_band = %u, n_rspwr = %.2f", i, arfcn, band, rxpwr_real);

        off += sizeof(GsmNeigAuxMeas);
    }

    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_l1_neig_aux_meas(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;
    uint8_t radio_id = p[0];
    DIAG_LOGD("GSM DSDS NEIG AUX MEAS: radio_id_pkt=%u", radio_id);
    return parse_gsm_l1_neig_aux_meas(p + 1, plen - 1);
}

static inline void unpack_lai(const uint8_t lai[5], uint16_t &mcc, uint16_t &mnc, uint16_t &lac) {
    // 3GPP TS 24.008 §10.5.1.3 LAI — BCD digits are stored with the FIRST
    // digit in the LOW nibble and the SECOND in the HIGH nibble (swapped):
    //   octet0: [MCC d2 | MCC d1]
    //   octet1: [MNC d3 | MCC d3]
    //   octet2: [MNC d2 | MNC d1]
    // Verified on SM8550 / MTS RU: 52 F0 10 → MCC=250 MNC=01.
    const uint8_t mcc_d1 = lai[0] & 0x0F;
    const uint8_t mcc_d2 = (lai[0] >> 4) & 0x0F;
    const uint8_t mcc_d3 = lai[1] & 0x0F;

    const uint8_t mnc_d3 = (lai[1] >> 4) & 0x0F;
    const uint8_t mnc_d1 = lai[2] & 0x0F;
    const uint8_t mnc_d2 = (lai[2] >> 4) & 0x0F;

    // Filler/unset LAI: when the modem reports a cell's CID/LAC but its PLMN
    // wasn't decoded (not camped), the MCC digits come as 0xF. Emit mcc=mnc=0
    // ("unknown") instead of garbage like 1665 — the cell then qualifies as a
    // lock-incomplete target so we can camp on it to recover the PLMN.
    if (mcc_d1 == 0x0F || mcc_d2 == 0x0F || mcc_d3 == 0x0F) {
        mcc = 0;
        mnc = 0;
        lac = static_cast<uint16_t>((static_cast<uint16_t>(lai[3]) << 8) | lai[4]);
        return;
    }

    mcc = static_cast<uint16_t>(mcc_d1 * 100 + mcc_d2 * 10 + mcc_d3);

    if (mnc_d3 == 0x0F) {
        // 2-digit MNC (0x0F filler in the 3rd digit)
        mnc = static_cast<uint16_t>(mnc_d1 * 10 + mnc_d2);
    } else {
        // 3-digit MNC
        mnc = static_cast<uint16_t>(mnc_d1 * 100 + mnc_d2 * 10 + mnc_d3);
    }

    // LAC: 16-bit BIG-endian (octet4 = MSB, octet5 = LSB).
    lac = static_cast<uint16_t>((static_cast<uint16_t>(lai[3]) << 8) | lai[4]);
}

// ─────────────────────────────────────────────────────────────────────────────
// C1/C2 — recompute for the serving cell whenever we have BOTH a measured
// RxLev (from 0x5134/burst metrics) AND the SI-3/SI-4 access parameters.
// ─────────────────────────────────────────────────────────────────────────────
// Compute a NEIGHBOUR cell's C1/C2 from its stored SI-3 cell-selection
// params (arfcn_sel_) + a measured RxLev. Returns false if params unknown.
bool DiagGsmLogParser::neighbor_c1c2(uint16_t arfcn, int16_t rxlev,
                                     int16_t &c1, int16_t &c2) {
    using namespace gsm_metrics;
    auto it = arfcn_sel_.find(arfcn);
    if (it == arfcn_sel_.end() || !it->second.valid || rxlev == 0) return false;
    const CellSelParams &sel = it->second;
    GsmBand band = gsm_band(arfcn);
    int _c1 = compute_c1(rxlev, sel.rxlev_access_min, sel.ms_txpwr_max_cch, band);
    int _c2 = compute_c2(_c1, sel.resel_present, sel.cell_reselect_off,
                         sel.temporary_offset, sel.penalty_time);
    c1 = static_cast<int16_t>(_c1);
    c2 = static_cast<int16_t>(_c2);
    DIAG_LOGD("GSM NBR C1/C2: arfcn=%u rxlev=%d accmin=%u txpwr=%u -> C1=%d C2=%d",
              arfcn, rxlev, sel.rxlev_access_min, sel.ms_txpwr_max_cch, _c1, _c2);
    return true;
}

void DiagGsmLogParser::recompute_serving_c1c2() {
    using namespace gsm_metrics;
    if (!serving_sel_.valid || serving_.arfcn == 0 || serving_.rxlev == 0) {
        return;// not enough info yet
    }
    GsmBand band = gsm_band(serving_.arfcn);
    int c1 = compute_c1(serving_.rxlev, serving_sel_.rxlev_access_min,
                        serving_sel_.ms_txpwr_max_cch, band);
    int c2 = compute_c2(c1, serving_sel_.resel_present,
                        serving_sel_.cell_reselect_off,
                        serving_sel_.temporary_offset,
                        serving_sel_.penalty_time);
    serving_.c1 = static_cast<int16_t>(c1);
    serving_.c2 = static_cast<int16_t>(c2);
    DIAG_LOGD("GSM C1/C2: arfcn=%u rxlev=%d accmin=%u txpwr=%u -> C1=%d C2=%d",
              serving_.arfcn, serving_.rxlev, serving_sel_.rxlev_access_min,
              serving_sel_.ms_txpwr_max_cch, c1, c2);
}

// ─────────────────────────────────────────────────────────────────────────────
// CELL INFO
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gsm_cell_info(const uint8_t *p, size_t plen) {
    // ── SHELL DEBUG (enable via debug_) ──────────────────────────────────
    // Dumps the RAW 0x5134 bytes. Layout is the classic 13-byte scat struct
    // <HBBH5sBB; the LAI nibble-order bug that produced MCC=535 is fixed.
    if (debug_) {
        char hexbuf[3 * 96 + 1];
        size_t hn = plen < 96 ? plen : 96;
        size_t o = 0;
        for (size_t k = 0; k < hn && o + 3 < sizeof(hexbuf); ++k)
            o += static_cast<size_t>(snprintf(hexbuf + o, sizeof(hexbuf) - o, "%02X ", p[k]));
        hexbuf[o ? o - 1 : 0] = '\0';
        DIAG_LOGD("GSM CELLINFO RAW plen=%zu bytes=[%s]", plen, hexbuf);
    }

    if (plen < sizeof(GsmCellInfo))
        return false;

    GsmCellInfo ci{};
    memcpy(&ci, p, sizeof(ci));

    uint16_t arfcn = ci.arfcn_band & 0x0FFF;
    uint16_t band = (ci.arfcn_band >> 12);

    uint16_t mcc = 0, mnc = 0, lac = 0;
    unpack_lai(ci.lai, mcc, mnc, lac);

    DIAG_LOGD(
            "GSM CELL INFO: ARFCN=%u BC=%u BCC=%u NCC=%u MCC=%u MNC=%u LAC=%u CID=%u priority=%u ncc_permitted=%u",
            arfcn,
            band,
            ci.bcc,
            ci.ncc,
            mcc,
            mnc,
            lac,
            ci.cid,
            ci.priority,
            ci.ncc_permitted);

    // Populate the serving cell and emit. 0x5134 (RR Cell Info) is GSM's
    // identity source — analogous to LTE SIB1. BSIC = NCC(3 bits)<<3 | BCC.
    //
    // Layout confirmed on SM8550 (13 bytes, classic scat <HBBH5sBB). The only
    // earlier bug was the LAI nibble order — now fixed in unpack_lai(). Keep a
    // light sanity check so a corrupt packet can't inject a bogus cell.
    const bool idSane = (mcc >= 100 && mcc <= 999) && (ci.cid != 0);
    serving_.arfcn = arfcn;
    serving_.band = static_cast<uint8_t>(band);
    serving_.serving = true;
    if (idSane) {
        serving_.bsic = static_cast<uint8_t>(((ci.ncc & 0x07) << 3) | (ci.bcc & 0x07));
        serving_.ncc = ci.ncc & 0x07;
        serving_.bcc = ci.bcc & 0x07;
        serving_.lac = lac;
        serving_.cid = ci.cid;
        serving_.mcc = mcc;
        serving_.mnc = mnc;
    } else {
        DIAG_LOGW("GSM CELL INFO: identity failed sanity (mcc=%u cid=%u) — skipped",
                  mcc, ci.cid);
    }
    // rxlev is filled separately by burst-metric handlers; keep any prior value.
    recompute_serving_c1c2();// in case SI-3 params already arrived
    if (cell_cb_) cell_cb_(serving_);

    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_cell_info(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;
    uint8_t radio_id = p[0];
    DIAG_LOGD("GSM DSDS CELL INFO: radio_id_pkt=%u", radio_id);
    return parse_gsm_cell_info(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// RR
// ─────────────────────────────────────────────────────────────────────────────

static void gsm_emit_journal(uint16_t code, const std::string &label,
                             const std::string &summary, const std::string &detail,
                             const uint8_t *p, size_t plen) {
    if (!journal_enabled()) return;
    JournalRecord jr;
    jr.t = static_cast<double>(time(nullptr));
    jr.code = code;
    jr.rat = "GSM";
    jr.channel = label;
    jr.msg_type = label;
    jr.summary = summary;
    jr.detail = detail;
    jr.raw = journal_hex(p, plen);
    jr.len = plen;
    journal_emit(jr);
}
bool DiagGsmLogParser::parse_gsm_rr(const uint8_t *p, size_t plen) {
    if (plen < sizeof(GsmRrHeader))
        return false;

    GsmRrHeader rr{};
    memcpy(&rr, p, sizeof(rr));

    size_t actual_len = plen - sizeof(rr);
    size_t l3_len = rr.msg_len;

    if (l3_len > actual_len)
        l3_len = actual_len;

    // Name the RR message type — System Information 1-6 carry the passive
    // GSM goldmine (SI3/SI6 = serving CID+LAI, SI2/SI2bis/SI5 = neighbour BA
    // ARFCN lists). This is the GSM analog of LTE SIB1 (0xB0C0). Capture the
    // RR-L3 hex for an SI3 to extend the parser to read identity/neighbours
    // straight from BCCH (faster passive fill, no operator switching needed).
    const char *si = nullptr;
    switch (rr.msg_type) {
        case 0x19:
            si = "SI-1";
            break;
        case 0x1A:
            si = "SI-2";
            break;
        case 0x02:
            si = "SI-2bis";
            break;
        case 0x03:
            si = "SI-2ter";
            break;
        case 0x1B:
            si = "SI-3";
            break;
        case 0x1C:
            si = "SI-4";
            break;
        case 0x1D:
            si = "SI-5";
            break;
        case 0x05:
            si = "SI-5bis";
            break;
        case 0x06:
            si = "SI-5ter";
            break;
        case 0x1E:
            si = "SI-6";
            break;
        default:
            si = nullptr;
            break;
    }

    DIAG_LOGD(
            "RR: chan=0x%02X type=0x%02X (%s) len=%zu",
            rr.chan_type_dir,
            rr.msg_type,
            si ? si : "other",
            l3_len);

    // Dump L3 only for System Information (the useful ones) to cut noise.
    if (si) hex_dump("RR-L3", p + sizeof(rr), l3_len, 64);
    std::string si_detail;

    // ── System Information Type 3 / 6 → CID + LAI (the GSM "SIB1") ──
    // BCCH layout (3GPP TS 44.018 §9.1.35/§9.1.40):
    //   l3[0] = L2 pseudo-length, l3[1] = PD/skip, l3[2] = message type,
    //   l3[3..4] = Cell Identity (big-endian),
    //   l3[5..9] = Location Area Identification (MCC/MNC/LAC).
    // SI-3 is broadcast ~1-2×/sec and the modem reads neighbour BCCHs too, so
    // this fills GSM identity FAST (vs the slower 0x5134) — no operator switch.
    if (rr.msg_type == 0x1B || rr.msg_type == 0x1E) {
        const uint8_t *l3 = p + sizeof(rr);
        if (l3_len >= 10) {
            uint16_t cid = static_cast<uint16_t>((l3[3] << 8) | l3[4]);
            uint16_t mcc = 0, mnc = 0, lac = 0;
            unpack_lai(l3 + 5, mcc, mnc, lac);

            // ── SI-3 Cell Selection Parameters (TS 44.018 §9.1.35) ──────────
            // Fixed layout: CI(2)@3, LAI(5)@5, CtrlChanDescr(3)@10, CellOpt@13,
            // Cell Selection Parameters(2)@14, RACH(3)@16, SI3 Rest Octets@19.
            gsm_metrics::CellSelParams si3_sel{};
            bool si3_sel_valid = false;
            if (rr.msg_type == 0x1B && l3_len >= 16) {
                gsm_metrics::parse_cell_selection_params(l3 + 14, si3_sel);
                if (l3_len > 19)
                    gsm_metrics::parse_si3_rest_octets(l3 + 19, l3_len - 19, si3_sel);
                si3_sel_valid = si3_sel.valid;
                // Distinguish serving vs neighbour SI-3 via the surround hint:
                // a neighbour BCCH is read right after GPRS_SURROUND_SEARCH_START
                // (hint fresh). Only OUR serving cell's SI-3 (no fresh hint)
                // updates the running serving params, so neighbours can't
                // corrupt serving C1/C2.
                if (!surround_hint_fresh_) {
                    serving_sel_ = si3_sel;
                    recompute_serving_c1c2();
                }
            }

            if (cid != 0 && mcc >= 100 && mcc <= 999) {
                DIAG_LOGD("GSM SI-%s: CID=%u LAC=%u MCC=%u MNC=%u",
                          rr.msg_type == 0x1B ? "3" : "6", cid, lac, mcc, mnc);
                {
                    char _b[112];
                    std::snprintf(_b, sizeof(_b), "CID=%u LAC=%u MCC=%u MNC=%u", cid, lac, mcc, mnc);
                    si_detail = _b;
                }
                GsmCell g{};
                g.cid = cid;
                g.lac = lac;
                g.mcc = mcc;
                g.mnc = mnc;
                g.bsic = 0xFF;// BSIC unknown from SI alone
                g.arfcn = 0;  // SI-3 carries no ARFCN
                // If this is the cell we're camped on, attach its ARFCN/BSIC/
                // rxlev/NCC/BCC/C1/C2 so it merges with the serving entry
                // instead of becoming a separate CID-keyed row.
                if (serving_.cid == cid && serving_.arfcn != 0) {
                    g.arfcn = serving_.arfcn;
                    g.band = serving_.band;
                    g.bsic = serving_.bsic;
                    g.ncc = serving_.ncc;
                    g.bcc = serving_.bcc;
                    g.rxlev = serving_.rxlev;
                    g.c1 = serving_.c1;
                    g.c2 = serving_.c2;
                    g.serving = true;
                } else if (surround_arfcn_hint_ != 0 && surround_hint_fresh_ &&
                           surround_arfcn_hint_ != serving_.arfcn) {
                    // Tight join: only the FIRST neighbour SI-3 after a
                    // GPRS_SURROUND_SEARCH_START consumes the ARFCN hint, and
                    // never the serving ARFCN — avoids a stale hint bleeding
                    // onto unrelated neighbour BCCH reads.
                    g.arfcn = surround_arfcn_hint_;
                    surround_hint_fresh_ = false;
                    // Remember this neighbour's BCCH cell-selection params so
                    // C1/C2 can be computed now or when its RxLev arrives later.
                    if (si3_sel_valid) arfcn_sel_[g.arfcn] = si3_sel;
                    {
                        auto _it = arfcn_rxlev_.find(g.arfcn);
                        int16_t nrx = (_it != arfcn_rxlev_.end()) ? _it->second : 0;
                        if (nrx == 0 && serving_.arfcn == g.arfcn) nrx = serving_.rxlev;
                        if (nrx != 0) {
                            g.rxlev = nrx;
                            int16_t _c1 = 0, _c2 = 0;
                            if (neighbor_c1c2(g.arfcn, nrx, _c1, _c2)) {
                                g.c1 = _c1;
                                g.c2 = _c2;
                            }
                        }
                    }
                    char _b2[48];
                    std::snprintf(_b2, sizeof(_b2), " arfcn=%u(surround)", surround_arfcn_hint_);
                    si_detail += _b2;
                }
                if (cell_cb_) cell_cb_(g);
            }
        }
    }

    // ── SI-4 (0x1C) also carries Cell Selection Parameters for the serving
    // cell (TS 44.018 §9.1.36): LAI(5)@3, Cell Selection Parameters(2)@8.
    // No Cell Identity in SI-4, so we treat it as serving-cell params only.
    if (rr.msg_type == 0x1C) {
        const uint8_t *l3 = p + sizeof(rr);
        if (l3_len >= 10) {
            gsm_metrics::parse_cell_selection_params(l3 + 8, serving_sel_);
            recompute_serving_c1c2();
            uint16_t mcc = 0, mnc = 0, lac = 0;
            unpack_lai(l3 + 3, mcc, mnc, lac);
            char _b[96];
            std::snprintf(_b, sizeof(_b), "LAI MCC=%u MNC=%u LAC=%u", mcc, mnc, lac);
            si_detail = _b;
        }
    }

    if (si) gsm_emit_journal(0x512F, std::string("GSM RR ") + si,
                             si_detail.empty() ? std::string(si) : si_detail,
                             si_detail, p + sizeof(rr), l3_len);
    return true;
}

bool DiagGsmLogParser::parse_gsm_dsds_rr(const uint8_t *p, size_t plen) {
    if (plen < 1)
        return false;

    return parse_gsm_rr(p + 1, plen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPRS OTA
// ─────────────────────────────────────────────────────────────────────────────
bool DiagGsmLogParser::parse_gprs_ota(const uint8_t *p, size_t plen) {
    if (plen < sizeof(GprsOtaHeader))
        return false;

    GprsOtaHeader h{};
    memcpy(&h, p, sizeof(h));

    size_t actual_len = plen - sizeof(h);
    size_t l3_len = h.msg_len;

    if (l3_len > actual_len)
        l3_len = actual_len;

    DIAG_LOGD(
            "GPRS OTA: dir=%u type=0x%02X len=%zu",
            h.msg_dir,
            h.msg_type,
            l3_len);

    hex_dump("GPRS-L3", p + sizeof(h), l3_len, 64);

    return true;
}