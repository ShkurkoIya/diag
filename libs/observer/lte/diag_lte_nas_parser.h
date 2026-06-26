#pragma once
#ifndef DIAG_LTE_NAS_PARSER_H
#define DIAG_LTE_NAS_PARSER_H

#include "diag_common.h"
#include "diag_lte_rrc_parser.h"// for LteCellIdentity

// ─────────────────────────────────────────────────────────────────────────────
// LTE NAS log codes (EMM + ESM only)
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_LTE_NAS_EMM_SEC_OTA_IN_C ((uint16_t) (0xB0EA))
#define LOG_LTE_NAS_EMM_SEC_OTA_OUT_C ((uint16_t) (0xB0EB))
#define LOG_LTE_NAS_EMM_PLAIN_OTA_IN_C ((uint16_t) (0xB0EC))
#define LOG_LTE_NAS_EMM_PLAIN_OTA_OUT_C ((uint16_t) (0xB0ED))
#define LOG_LTE_NAS_ESM_SEC_OTA_IN_C ((uint16_t) (0xB0E0))
#define LOG_LTE_NAS_ESM_PLAIN_OTA_IN_C ((uint16_t) (0xB0E2))

// ─────────────────────────────────────────────────────────────────────────────
// DiagLteNasParser — handles NAS-layer log codes (EMM/ESM only)
//
// Code     | Function           | Yields
// ─────────┼────────────────────┼────────────────────────────────
// 0xB0EA   | parse_nas_emm_ota  | (Attach/TAU Accept TAI list)
// 0xB0EC   | parse_nas_emm_ota  | (Attach/TAU Accept TAI list) ★
// 0xB0ED   | parse_nas_emm_ota  | outgoing — usually no TAI
//
// Produces partial LteCellIdentity with only {mcc, mnc, tac} populated.
// This is a FALLBACK source. Primary identity comes from DiagLteRrcParser
// via 0xB0C2.
// ─────────────────────────────────────────────────────────────────────────────
class DiagLteNasParser {
public:
    using IdentityCallback = std::function<void(const LteCellIdentity &)>;

    DiagLteNasParser() = default;

    void set_identity_callback(IdentityCallback cb) { id_cb_ = std::move(cb); }
    void set_debug(bool on) { debug_ = on; }

    static std::vector<uint16_t> handled_log_codes();
    bool parse(const uint8_t *buf, size_t len);

    const LteCellIdentity &identity() const { return id_; }

private:
    bool parse_nas_emm_ota(const uint8_t *p, size_t plen);

    bool extract_tai_from_nas(const uint8_t *nas_msg, size_t nas_len);
    bool parse_tai_list(const uint8_t *tai, size_t len);

    // Decode BCD PLMN (3 bytes → MCC + MNC) — NAS uses BCD encoding
    // (different from 0xB0C2 which uses decimal integer fields)
    static void decode_plmn(const uint8_t *plmn_bytes, uint16_t &mcc, uint16_t &mnc);

    LteCellIdentity id_;// only mcc/mnc/tac filled
    IdentityCallback id_cb_;
    bool debug_ = true;
};

#endif// DIAG_LTE_NAS_PARSER_H
