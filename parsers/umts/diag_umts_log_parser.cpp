#include "diag_umts_log_parser.h"
#include <cstring>

using namespace diag_util;

// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint16_t> DiagUmtsLogParser::handled_log_codes() {
    return {
            LOG_UMTS_NAS_OTA_C,
            LOG_UMTS_NAS_GMMSM_OTA_C,
            LOG_UMTS_NAS_MM_STATE_C,
            LOG_UMTS_RRC_OTA_C,
            LOG_UMTS_RRC_STATES_C,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
bool DiagUmtsLogParser::parse(const uint8_t* buf, size_t len) {
    if (len < sizeof(LogRecord)) {
        DIAG_LOGW("UMTS: packet too short (%zu)", len);
        return false;
    }
    const auto*    hdr  = reinterpret_cast<const LogRecord*>(buf);
    const uint8_t* p    = buf + sizeof(LogRecord);
    const size_t   plen = len - sizeof(LogRecord);

    switch (hdr->code) {
        case LOG_UMTS_NAS_OTA_C:
        case LOG_UMTS_NAS_GMMSM_OTA_C:
            return parse_nas_ota(p, plen);

        case LOG_UMTS_RRC_OTA_C:
            return parse_rrc_ota(p, plen);

        case LOG_UMTS_NAS_MM_STATE_C:
            return parse_mm_state(p, plen);

        default:
            return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x713A / 0x7130 – NAS OTA message
//
// Wire format:
//   UmtsNasOtaHeader (direction + nas_hdr_len + msg_len=4)
//   [nas_hdr_len bytes of optional NAS header]
//   msg_len bytes of NAS PDU
// ─────────────────────────────────────────────────────────────────────────────
bool DiagUmtsLogParser::parse_nas_ota(const uint8_t* p, size_t plen) {
    const auto* hdr = as_struct<UmtsNasOtaHeader>(p, plen);
    if (!hdr) {
        DIAG_LOGW("UMTS NAS OTA: payload too short (%zu)", plen);
        return false;
    }

    size_t hdr_sz  = sizeof(UmtsNasOtaHeader);
    size_t nas_off = hdr_sz + hdr->nas_hdr_len;
    uint32_t msg_len = hdr->msg_len;

    if (plen < nas_off + msg_len) {
        DIAG_LOGW("UMTS NAS OTA: truncated (need %zu have %zu)",
                  nas_off + msg_len, plen);
        // Proceed with what we have
        if (plen <= nas_off) return false;
        msg_len = static_cast<uint32_t>(plen - nas_off);
    }

    const uint8_t* pdu = p + nas_off;

    // Parse the NAS header (first 2 bytes: PD + msg_type)
    uint8_t pd       = 0;
    uint8_t msg_type = 0;
    if (msg_len >= 2) {
        pd       = pdu[0] & 0x0F;  // lower nibble = protocol discriminator
        msg_type = pdu[1];
    }

    UmtsNasMessage msg{};
    msg.is_uplink = (hdr->direction == NAS_UL);
    msg.pd        = pd;
    msg.msg_type  = msg_type;
    msg.pdu.assign(pdu, pdu + msg_len);

    DIAG_LOGD("UMTS NAS %s PD=0x%02X MsgType=0x%02X (%s) len=%u",
              msg.is_uplink ? "UL" : "DL",
              pd, msg_type,
              nas_msg_name(pd, msg_type),
              msg_len);

    if (nas_cb_) nas_cb_(msg);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x712F – UMTS RRC OTA (raw ASN.1 PER PDU)
// ─────────────────────────────────────────────────────────────────────────────
bool DiagUmtsLogParser::parse_rrc_ota(const uint8_t* p, size_t plen) {
    const auto* hdr = as_struct<UmtsRrcOtaHeader>(p, plen);
    if (!hdr) {
        DIAG_LOGW("UMTS RRC OTA: payload too short (%zu)", plen);
        return false;
    }

    size_t   off  = sizeof(UmtsRrcOtaHeader);
    uint16_t mlen = hdr->msg_len;

    if (plen < off + mlen) {
        DIAG_LOGW("UMTS RRC OTA: truncated (need %zu have %zu)", off + mlen, plen);
        mlen = static_cast<uint16_t>(plen > off ? plen - off : 0);
    }

    UmtsRrcMessage msg{};
    msg.channel = hdr->channel_type;
    msg.rb_id   = hdr->rb_id;
    msg.pdu.assign(p + off, p + off + mlen);

    DIAG_LOGD("UMTS RRC %s RB=%u len=%u",
              rrc_channel_name(hdr->channel_type), hdr->rb_id, mlen);

    if (rrc_cb_) rrc_cb_(msg);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x7131 – MM state
// ─────────────────────────────────────────────────────────────────────────────
bool DiagUmtsLogParser::parse_mm_state(const uint8_t* p, size_t plen) {
    const auto* s = as_struct<UmtsMmState>(p, plen);
    if (!s) return false;

    DIAG_LOGD("UMTS MM state=%u substate=%u update_status=%u",
              s->mm_state, s->mm_substate, s->mm_update_status);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// NAS message name tables  (3GPP TS 24.008)
// ─────────────────────────────────────────────────────────────────────────────
const char* DiagUmtsLogParser::nas_msg_name(uint8_t pd, uint8_t msg_type) {
    // GMM  PD=0x08
    if (pd == PD_GMM) {
        switch (msg_type) {
            case 0x01: return "Attach Request";
            case 0x02: return "Attach Accept";
            case 0x03: return "Attach Complete";
            case 0x04: return "Attach Reject";
            case 0x05: return "Detach Request";
            case 0x06: return "Detach Accept";
            case 0x08: return "RAU Request";
            case 0x09: return "RAU Accept";
            case 0x0A: return "RAU Complete";
            case 0x0B: return "RAU Reject";
            case 0x10: return "P-TMSI Realloc Command";
            case 0x11: return "P-TMSI Realloc Complete";
            case 0x12: return "Auth & Cipher Request";
            case 0x13: return "Auth & Cipher Response";
            case 0x14: return "Auth & Cipher Reject";
            case 0x1C: return "Identity Request";
            case 0x1D: return "Identity Response";
            case 0x20: return "GMM Status";
            case 0x21: return "GMM Information";
            default:   return "GMM Unknown";
        }
    }
    // MM  PD=0x05
    if (pd == PD_MM) {
        switch (msg_type) {
            case 0x01: return "IMSI Detach Indication";
            case 0x02: return "Location Updating Accept";
            case 0x04: return "Location Updating Reject";
            case 0x08: return "Location Updating Request";
            case 0x11: return "Auth Request";
            case 0x12: return "Auth Response";
            case 0x14: return "Identity Request";
            case 0x19: return "Identity Response";
            case 0x1A: return "IMSI Detach";
            case 0x21: return "CM Service Accept";
            case 0x22: return "CM Service Reject";
            case 0x23: return "CM Service Abort";
            case 0x24: return "CM Service Request";
            case 0x25: return "CM Service Prompt";
            case 0x29: return "CM Re-establishment Request";
            case 0x2A: return "Abort";
            case 0x30: return "MM Null";
            case 0x31: return "MM Status";
            case 0x32: return "MM Information";
            default:   return "MM Unknown";
        }
    }
    // SM  PD=0x0A
    if (pd == PD_SM) {
        switch (msg_type) {
            case 0x41: return "Activate PDP Context Request";
            case 0x42: return "Activate PDP Context Accept";
            case 0x43: return "Activate PDP Context Reject";
            case 0x46: return "Deactivate PDP Context Request";
            case 0x47: return "Deactivate PDP Context Accept";
            case 0x48: return "Modify PDP Context Request (UE)";
            case 0x49: return "Modify PDP Context Accept";
            case 0x55: return "SM Status";
            default:   return "SM Unknown";
        }
    }
    return "Unknown";
}

const char* DiagUmtsLogParser::rrc_channel_name(uint8_t ch) {
    switch (ch) {
        case RRC_DL_BCCH: return "DL-BCCH";
        case RRC_DL_CCCH: return "DL-CCCH";
        case RRC_DL_DCCH: return "DL-DCCH";
        case RRC_UL_CCCH: return "UL-CCCH";
        case RRC_UL_DCCH: return "UL-DCCH";
        case RRC_DL_PCCH: return "DL-PCCH";
        case RRC_UL_RACH: return "UL-RACH";
        default:          return "UNKNOWN";
    }
}