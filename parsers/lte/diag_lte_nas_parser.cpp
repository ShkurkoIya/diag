#include "diag_lte_nas_parser.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

using namespace diag_util;

// ═════════════════════════════════════════════════════════════════════════════
// Log code registration — NAS only (no more RRC here, that moved to RRC parser)
// ═════════════════════════════════════════════════════════════════════════════
std::vector<uint16_t> DiagLteNasParser::handled_log_codes() {
    return {
        LOG_LTE_NAS_EMM_PLAIN_OTA_IN_C,   // 0xB0EC ★ Attach Accept arrives here
        LOG_LTE_NAS_EMM_PLAIN_OTA_OUT_C,  // 0xB0ED
        LOG_LTE_NAS_EMM_SEC_OTA_IN_C,     // 0xB0EA
        // Uncomment as you add ESM parsing:
        // LOG_LTE_NAS_EMM_SEC_OTA_OUT_C,
        // LOG_LTE_NAS_ESM_SEC_OTA_IN_C,
        // LOG_LTE_NAS_ESM_PLAIN_OTA_IN_C,
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// Dispatch (RRC codes removed — they now live in DiagLteRrcParser)
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteNasParser::parse(const uint8_t* buf, size_t len) {
    if (len < sizeof(LogRecord)) return false;
    const auto* hdr = reinterpret_cast<const LogRecord*>(buf);
    const uint8_t* p = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);

    switch (hdr->code) {
        case LOG_LTE_NAS_EMM_PLAIN_OTA_IN_C:
        case LOG_LTE_NAS_EMM_PLAIN_OTA_OUT_C:
        case LOG_LTE_NAS_EMM_SEC_OTA_IN_C:
            return parse_nas_emm_ota(p, plen);
        default:
            DIAG_LOGD("LTE NAS: unknown code 0x%04X", hdr->code);
            return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// NAS EMM OTA  (3GPP TS 24.301)
//
// DIAG log payload:
//   [0]    ext_header_version
//   [1]    rrc_release_number
//   [2]    rrc_version_release
//   [3]    bearer_id (or reserved)
//   [4-7]  nas_msg_length (uint32 LE)
//   [8+]   NAS PDU
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteNasParser::parse_nas_emm_ota(const uint8_t* p, size_t plen) {
    if (plen < 10) return false;

    uint32_t nas_len = rd32(p + 4);
    const uint8_t* nas_msg = p + 8;
    size_t nas_avail = plen - 8;

    if (nas_len > nas_avail) nas_len = static_cast<uint32_t>(nas_avail);
    if (nas_len < 2) return false;

    if (debug_) {
        DIAG_LOGD("LTE NAS EMM: nas_len=%u", nas_len);
        hex_dump("NAS_PDU", nas_msg, nas_len, 48);
    }

    return extract_tai_from_nas(nas_msg, nas_len);
}

// ═════════════════════════════════════════════════════════════════════════════
// Extract TAI from NAS Attach Accept (msg_type 0x42) or TAU Accept (0x49)
// (3GPP TS 24.301 §9.9.3.33)
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteNasParser::extract_tai_from_nas(const uint8_t* msg, size_t len) {
    if (len < 2) return false;

    uint8_t pd       = msg[0] & 0x0F;
    uint8_t sec_hdr  = (msg[0] >> 4);
    uint8_t msg_type = msg[1];

    if (pd != 0x07) return false;  // not EMM

    const uint8_t* body = msg + 2;
    size_t body_len = len - 2;

    if (sec_hdr != 0 && len > 7) {
        // Security-protected — can't decode the inner NAS without keys
        return false;
    }

    if (debug_)
        DIAG_LOGD("NAS EMM: PD=0x%X sec=%u msg_type=0x%02X body_len=%zu",
                  pd, sec_hdr, msg_type, body_len);

    if (msg_type == 0x42) {
        // Attach Accept
        if (body_len < 7) return false;
        uint8_t tai_list_len = body[2];
        const uint8_t* tai_list = body + 3;
        if (tai_list_len < 6 || static_cast<size_t>(tai_list_len) > body_len - 3) return false;
        return parse_tai_list(tai_list, tai_list_len);
    }

    if (msg_type == 0x49) {
        // TAU Accept — TAI list has IEI 0x54
        size_t off = 1;
        while (off + 2 < body_len) {
            uint8_t iei = body[off];
            if (iei == 0x54) {
                uint8_t tai_len = body[off + 1];
                if (off + 2 + tai_len <= body_len && tai_len >= 6)
                    return parse_tai_list(body + off + 2, tai_len);
                break;
            }
            // Skip Type-1 (single-byte) IEs
            if ((iei & 0xF0) >= 0x80) {
                off += 1;
                continue;
            }
            // Type 3/4 TLV
            if (off + 1 < body_len) {
                uint8_t ie_len = body[off + 1];
                off += 2 + ie_len;
            } else break;
        }
    }
    return false;
}

bool DiagLteNasParser::parse_tai_list(const uint8_t* tai, size_t len) {
    if (len < 6) return false;

    uint8_t type_of_list = (tai[0] >> 5) & 0x03;
    uint8_t num_elements = (tai[0] & 0x1F) + 1;

    if (debug_)
        DIAG_LOGD("TAI list: type=%u num_elements=%u", type_of_list, num_elements);

    uint16_t mcc = 0, mnc = 0, tac = 0;

    if (type_of_list == 0 || type_of_list == 1 || type_of_list == 2) {
        if (len < 6) return false;
        decode_plmn(tai + 1, mcc, mnc);
        tac = (static_cast<uint16_t>(tai[4]) << 8) | tai[5];
    }

    if (mcc > 0 && tac > 0) {
        id_.mcc   = mcc;
        id_.mnc   = mnc;
        id_.tac   = tac;
        id_.valid = true;

        DIAG_LOGI("NAS identity: MCC=%u MNC=%u TAC=0x%04X", mcc, mnc, tac);
        if (id_cb_) id_cb_(id_);
        return true;
    }
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// Decode BCD-encoded PLMN (3 bytes → MCC + MNC) per 3GPP TS 24.008 §10.5.1.3
// NOTE: this differs from 0xB0C2 which stores MCC/MNC as decimal integers.
// ═════════════════════════════════════════════════════════════════════════════
void DiagLteNasParser::decode_plmn(const uint8_t* b, uint16_t& mcc, uint16_t& mnc) {
    uint8_t mcc_d1 = b[0] & 0x0F;
    uint8_t mcc_d2 = (b[0] >> 4) & 0x0F;
    uint8_t mcc_d3 = b[1] & 0x0F;
    uint8_t mnc_d3 = (b[1] >> 4) & 0x0F;
    uint8_t mnc_d1 = b[2] & 0x0F;
    uint8_t mnc_d2 = (b[2] >> 4) & 0x0F;

    mcc = mcc_d1 * 100 + mcc_d2 * 10 + mcc_d3;

    if (mnc_d3 == 0x0F) {
        // 2-digit MNC
        mnc = mnc_d1 * 10 + mnc_d2;
    } else {
        // 3-digit MNC
        mnc = mnc_d1 * 100 + mnc_d2 * 10 + mnc_d3;
    }
}
