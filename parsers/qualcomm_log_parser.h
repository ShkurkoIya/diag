#pragma once
#ifndef QUALCOMM_LOG_PARSER_H
#define QUALCOMM_LOG_PARSER_H

#include "diag_common.h"

// Per-RAT parsers
#include "diag_gsm_log_parser.h"
#include "diag_wcdma_log_parser.h"
#include "diag_lte_rrc_parser.h"   // ★ NEW — identity from 0xB0C0/C1/C2
#include "diag_lte_nas_parser.h"   //   slimmed — EMM/ESM only
#include "diag_lte_ml1_parser.h"   //   renamed from diag_lte_log_parser
#include "diag_umts_log_parser.h"
#include "diag_nr_log_parser.h"

#include <unordered_map>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Radio Access Technology tag.
//
// LTE is split into three parsers with separate responsibilities:
//   LTE_RRC — cell identity (CID/TAC/MCC/MNC/PCI/EARFCN/Band/BW)  ★ authoritative
//   LTE_NAS — fallback identity (MCC/MNC/TAC from NAS Attach Accept)
//   LTE_ML1 — measurements (RSRP/RSRQ/RSSI/SINR)
// ─────────────────────────────────────────────────────────────────────────────
enum class Rat {
    GSM, WCDMA, UMTS, NR,
    LTE_RRC,   // ★ NEW
    LTE_NAS,
    LTE,       // ML1 measurements
    UNKNOWN
};

// ─────────────────────────────────────────────────────────────────────────────
// A single parsed DIAG frame as delivered to user callbacks
// ─────────────────────────────────────────────────────────────────────────────
struct DiagFrame {
    uint16_t       log_code;
    uint64_t       timestamp_raw;
    double         timestamp_unix;
    Rat            rat;
    const uint8_t* payload;
    size_t         payload_len;
};

// ─────────────────────────────────────────────────────────────────────────────
// QualcommLogParser — top-level dispatcher across all RATs
// ─────────────────────────────────────────────────────────────────────────────
class QualcommLogParser {
public:
    using NeighborCallback  = std::function<void(const ParsedNeighbors&)>;
    using RawFrameCallback  = std::function<void(const DiagFrame&)>;
    using NasCallback       = DiagUmtsLogParser::NasCallback;
    using RrcCallback       = DiagUmtsLogParser::RrcCallback;

    QualcommLogParser();
    ~QualcommLogParser() = default;

    QualcommLogParser(const QualcommLogParser&)            = delete;
    QualcommLogParser& operator=(const QualcommLogParser&) = delete;

    // ── Register callbacks ─────────────────────────────────────────
    void set_neighbor_callback(NeighborCallback cb) { neighbor_cb_ = std::move(cb); }
    void set_raw_frame_callback(RawFrameCallback cb) { raw_cb_ = std::move(cb); }
    void set_nas_callback(NasCallback cb);
    void set_rrc_callback(RrcCallback cb);

    // ── Feed data ──────────────────────────────────────────────────
    bool on_log(const uint8_t* buf, size_t len);
    bool on_event(const uint8_t* buf, size_t len);

    // ── Accessors ──────────────────────────────────────────────────
    const GsmCell&               gsm_serving()    const;
    const WcdmaCell&             wcdma_serving()  const;
    const LteCell&               lte_serving()    const;
    const NrCell&                nr_serving()     const;
    const std::vector<GsmCell>&  gsm_neighbors()  const;
    const std::vector<WcdmaCell>&wcdma_neighbors()const;
    const std::vector<LteCell>&  lte_neighbors()  const;
    const std::vector<NrCell>&   nr_neighbors()   const;

    // Direct parser accessors
    const DiagLteRrcParser& lte_rrc() const { return lte_rrc_; }
    const DiagLteNasParser& lte_nas() const { return lte_nas_; }
    const DiagLteMl1Parser& lte_ml1() const { return lte_ml1_; }

    // Authoritative LTE identity (RRC preferred, NAS fallback)
    const LteCellIdentity& lte_identity() const {
        return lte_rrc_.identity().valid ? lte_rrc_.identity() : lte_nas_.identity();
    }

    // Static helpers
    static std::vector<uint16_t> all_log_codes();
    static const char*           log_code_name(uint16_t code);
    static Rat                   log_code_rat(uint16_t code);

private:
    bool dispatch_log(uint16_t code, const uint8_t* buf, size_t len, uint64_t ts);
    void fire_neighbor_update();

    // Cross-reference: match ML1 measurements with RRC identity by EARFCN+PCI,
    // mark the matching cell as serving and stamp it with identity fields.
    void merge_lte_identity();

    // Sub-parsers
    DiagGsmLogParser    gsm_;
    DiagWcdmaLogParser  wcdma_;
    DiagLteRrcParser    lte_rrc_;    // ★ NEW
    DiagLteNasParser    lte_nas_;
    DiagLteMl1Parser    lte_ml1_;    // (was lte_)
    DiagUmtsLogParser   umts_;
    DiagNrLogParser     nr_;

    std::unordered_map<uint16_t, Rat> code_to_rat_;

    NeighborCallback neighbor_cb_;
    RawFrameCallback raw_cb_;
};

#endif // QUALCOMM_LOG_PARSER_H
