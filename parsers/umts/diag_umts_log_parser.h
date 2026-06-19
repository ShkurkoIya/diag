#pragma once
#ifndef DIAG_UMTS_LOG_PARSER_H
#define DIAG_UMTS_LOG_PARSER_H

#include "diag_common.h"

// ─────────────────────────────────────────────────────────────────────────────
// UMTS log codes  (LOG_UMTS_BASE_C = 0x7000)
// NAS/RRC messages are the primary UMTS-specific interest.
// Mirrors SCAT diagumtslogparser.py
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_UMTS_NAS_OTA_C              ((uint16_t)(0x713A))  // NAS OTA message
#define LOG_UMTS_NAS_GMMSM_OTA_C        ((uint16_t)(0x7130))  // GMM/SM OTA
#define LOG_UMTS_NAS_MM_STATE_C         ((uint16_t)(0x7131))  // MM state machine
#define LOG_UMTS_NAS_REG_C              ((uint16_t)(0x7132))  // Registration
#define LOG_UMTS_NAS_MSG_PARSE_C        ((uint16_t)(0x7136))
#define LOG_UMTS_CALL_CONTROL_C         ((uint16_t)(0x7150))  // CC (3GPP TS 24.008)
#define LOG_UMTS_RRC_OTA_C              ((uint16_t)(0x712F))  // RRC OTA raw (ASN.1)
#define LOG_UMTS_RRC_STATES_C           ((uint16_t)(0x7200))  // RRC state transitions
#define LOG_UMTS_DS_GPS_C               ((uint16_t)(0x7300))

// ─────────────────────────────────────────────────────────────────────────────
// NAS channel types (for decoding OTA direction)
// ─────────────────────────────────────────────────────────────────────────────
enum UmtsNasChannel : uint8_t {
    NAS_UL = 0,   // uplink   (UE → network)
    NAS_DL = 1,   // downlink (network → UE)
};

// ─────────────────────────────────────────────────────────────────────────────
// NAS message type IDs (3GPP TS 24.008 protocol discriminators)
// ─────────────────────────────────────────────────────────────────────────────
enum NasProtDiscriminator : uint8_t {
    PD_GMM = 0x08,  // GPRS Mobility Management
    PD_SM  = 0x0A,  // Session Management
    PD_MM  = 0x05,  // Mobility Management
    PD_CC  = 0x03,  // Call Control
    PD_SMS = 0x09,  // Short Message Service
    PD_SS  = 0x0B,  // Supplementary Services
};

// ─────────────────────────────────────────────────────────────────────────────
// Packed UMTS structures
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)

// 0x713A / 0x7130 – NAS OTA
struct UmtsNasOtaHeader {
    uint8_t  direction;    // 0 = UL, 1 = DL
    uint8_t  nas_hdr_len;  // length of nas_header field below (usually 0)
    uint32_t msg_len;      // actual NAS PDU length (little-endian)
    // uint8_t msg[msg_len]  follows
};

// NAS message header (first 2 bytes of any NAS PDU in 24.008)
struct NasMsgHeader {
    uint8_t  pd;           // protocol discriminator (lower 4 bits) + TI (upper 4)
    uint8_t  msg_type;     // message type
};

// 0x712F – UMTS RRC OTA
struct UmtsRrcOtaHeader {
    uint8_t  channel_type; // 0 = DL-BCCH, 1 = DL-CCCH, 2 = DL-DCCH, 3 = UL-CCCH, 4 = UL-DCCH
    uint8_t  rb_id;
    uint16_t msg_len;      // ASN.1 PER encoded RRC PDU length
    // uint8_t msg[msg_len] follows
};

// RRC channel type codes
enum UmtsRrcChannel : uint8_t {
    RRC_DL_BCCH = 0,
    RRC_DL_CCCH = 1,
    RRC_DL_DCCH = 2,
    RRC_UL_CCCH = 3,
    RRC_UL_DCCH = 4,
    RRC_DL_PCCH = 5,
    RRC_UL_RACH = 6,
};

// 0x7131 – MM state
struct UmtsMmState {
    uint8_t  mm_state;
    uint8_t  mm_substate;
    uint8_t  mm_update_status;
};

#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// Parsed UMTS NAS / RRC message record
// ─────────────────────────────────────────────────────────────────────────────
struct UmtsNasMessage {
    bool     is_uplink;
    uint8_t  pd;
    uint8_t  msg_type;
    std::vector<uint8_t> pdu;   // raw NAS PDU bytes
};

struct UmtsRrcMessage {
    uint8_t  channel;           // UmtsRrcChannel
    uint8_t  rb_id;
    std::vector<uint8_t> pdu;   // raw RRC PDU bytes (ASN.1 PER)
};

// ─────────────────────────────────────────────────────────────────────────────
class DiagUmtsLogParser {
public:
    using NasCallback = std::function<void(const UmtsNasMessage&)>;
    using RrcCallback = std::function<void(const UmtsRrcMessage&)>;

    DiagUmtsLogParser() = default;
    ~DiagUmtsLogParser() = default;

    void set_nas_callback(NasCallback cb) { nas_cb_ = std::move(cb); }
    void set_rrc_callback(RrcCallback cb) { rrc_cb_ = std::move(cb); }

    static std::vector<uint16_t> handled_log_codes();

    bool parse(const uint8_t* buf, size_t len);

    // Human-readable string for a NAS message type given PD + msg_type
    static const char* nas_msg_name(uint8_t pd, uint8_t msg_type);

    // Human-readable RRC channel name
    static const char* rrc_channel_name(uint8_t ch);

private:
    bool parse_nas_ota    (const uint8_t* p, size_t plen);
    bool parse_rrc_ota    (const uint8_t* p, size_t plen);
    bool parse_mm_state   (const uint8_t* p, size_t plen);

    NasCallback nas_cb_;
    RrcCallback rrc_cb_;
};

#endif // DIAG_UMTS_LOG_PARSER_H