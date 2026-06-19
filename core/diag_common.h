#pragma once
#ifndef DIAG_COMMON_H
#define DIAG_COMMON_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Android log tag
// ─────────────────────────────────────────────────────────────────────────────
#define DIAG_TAG "DiagParser"

#ifdef __ANDROID__
#  include <android/log.h>
#  define DIAG_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  DIAG_TAG, __VA_ARGS__)
#  define DIAG_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  DIAG_TAG, __VA_ARGS__)
#  define DIAG_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, DIAG_TAG, __VA_ARGS__)
#  define DIAG_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, DIAG_TAG, __VA_ARGS__)
#else
#  include <cstdio>
#  define DIAG_LOGI(...) do { printf("[I/" DIAG_TAG "] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#  define DIAG_LOGW(...) do { printf("[W/" DIAG_TAG "] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#  define DIAG_LOGE(...) do { printf("[E/" DIAG_TAG "] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#  define DIAG_LOGD(...) do { printf("[D/" DIAG_TAG "] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Top-level DIAG command codes  (from diagcmd.h)
// ─────────────────────────────────────────────────────────────────────────────
#define DIAG_LOG_F              0x10   // 16  – log packet
#define DIAG_EVENT_REPORT_F     0x60   // 96  – event report
#define DIAG_EXT_MSG_F          0x79   // 121 – QSR/extended message
#define DIAG_SUBSYS_CMD_F       0x4B   // 75  – subsystem command
#define DIAG_SUBSYS_CMD_VER_2_F 0x80   // 128 – subsystem command v2

// ─────────────────────────────────────────────────────────────────────────────
// Log equipment IDs / base codes
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_1X_BASE_C    ((uint16_t) 0x1000)
#define LOG_WCDMA_BASE_C ((uint16_t) 0x4000)
#define LOG_GSM_BASE_C   ((uint16_t) 0x5000)
#define LOG_UMTS_BASE_C  ((uint16_t) 0x7000)
#define LOG_LTE_BASE_C   ((uint16_t) 0xB010)
#define LOG_LTE_LAST_C   ((uint16_t) 0xB1FF)
#define LOG_NR_BASE_C    ((uint16_t) 0xB800)
#define LOG_NR_LAST_C    ((uint16_t) 0xB9FF)

// ─────────────────────────────────────────────────────────────────────────────
// DIAG log record header  (struct sLogRecord in Qualcomm.txt)
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)

struct DiagLogHeader {
    uint8_t  cmd_code;   // DIAG_LOG_F = 0x10
    uint8_t  more;       // reserved / more-data flag
    uint16_t len;        // length of log_record that follows
    uint16_t pad;        // unused padding
};

struct LogRecord {
    uint16_t len;        // full size: header (12 bytes) + payload
    uint16_t code;       // log code, e.g. 0x4111, 0xB192
    uint64_t timestamp;  // QCT epoch timestamp (1/32768 s ticks)
    // uint8_t data[0];  – variable-length payload starts here
};

// DIAG subsystem command header
struct DiagSubsysHeader {
    uint8_t  cmd_code;    // DIAG_SUBSYS_CMD_F = 0x4B
    uint8_t  subsys_id;   // DIAG_SUBSYS_xxx
    uint16_t subsys_cmd;  // sub-command (LE)
};

// Generic event record
struct DiagEventRecord {
    uint16_t event_id;
    uint64_t timestamp;
    uint8_t  payload_len;
    // uint8_t payload[payload_len];
};

#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// Parsed cell info structures – EXTENDED with identity fields
//
// Each cell type carries TAC/LAC, CID, and MCCMNC where available.
// Fields set to 0 or -1 mean "unknown / not yet filled".
// ─────────────────────────────────────────────────────────────────────────────

struct GsmCell {
    uint16_t arfcn    = 0;
    uint8_t  band     = 0;    // Qualcomm band-class nibble (arfcn_band>>12)
    uint8_t  bsic     = 0;    // 0xFF = unknown (no SCH sync)
    uint8_t  ncc      = 0xFF; // 0xFF = unknown  (BSIC>>3)
    uint8_t  bcc      = 0xFF; // 0xFF = unknown  (BSIC&7)
    int16_t  rxlev    = 0;    // dBm
    int16_t  c1       = INT16_MIN; // INT16_MIN = unknown (needs rxlev + SI-3 params)
    int16_t  c2       = INT16_MIN; // INT16_MIN = unknown
    uint16_t lac      = 0;
    uint16_t cid      = 0;
    uint32_t mcc      = 0;    // e.g. 250
    uint32_t mnc      = 0;    // e.g. 20
    bool     serving  = false;
};

struct WcdmaCell {
    uint16_t uarfcn   = 0;
    uint16_t psc      = 0;
    int16_t  rscp     = 0;    // dBm
    int16_t  ecno     = 0;    // dB  (Ec/No)
    uint16_t lac      = 0;
    uint32_t cid      = 0;
    uint32_t mcc      = 0;
    uint32_t mnc      = 0;
    bool     serving  = false;
};

struct LteCell {
    uint32_t earfcn   = 0;
    uint16_t pci      = 0;
    int16_t  rsrp     = 0;    // dBm
    int16_t  rsrq     = 0;    // dB
    int16_t  rssi     = 0;    // dBm
    int32_t  cell_id  = -1;   // 28-bit E-UTRAN Cell ID, -1 = unknown
    uint16_t tac      = 0;    // Tracking Area Code
    uint32_t mcc      = 0;
    uint32_t mnc      = 0;
    uint8_t  dl_bw    = 0;    // DL bandwidth (raw B0C2 field; 0 = unknown)
    uint8_t  ul_bw    = 0;    // UL bandwidth (raw B0C2 field; 0 = unknown)
    bool     serving  = false;
};

struct UmtsCell {
    uint16_t uarfcn   = 0;
    uint16_t psc      = 0;
    int16_t  rscp     = 0;
    int16_t  ecno     = 0;
    uint16_t lac      = 0;
    uint32_t cid      = 0;
    uint32_t mcc      = 0;
    uint32_t mnc      = 0;
    bool     serving  = false;
};

struct NrCell {
    uint32_t nrarfcn  = 0;    // NR-ARFCN (up to ~3279165)
    uint16_t pci      = 0;    // 0-1007
    int16_t  ss_rsrp  = 0;    // SS-RSRP in dBm
    int16_t  ss_rsrq  = 0;    // SS-RSRQ in dB
    int16_t  ss_sinr  = 0;    // SS-SINR in dB
    int32_t  cell_id  = -1;
    uint32_t tac      = 0;
    uint32_t mcc      = 0;
    uint32_t mnc      = 0;
    bool     serving  = false;
};

// Aggregate parsed result returned to callers
struct ParsedNeighbors {
    std::vector<GsmCell>   gsm;
    std::vector<WcdmaCell> wcdma;
    std::vector<LteCell>   lte;
    std::vector<UmtsCell>  umts;
    std::vector<NrCell>    nr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Timestamp conversion helper
// ─────────────────────────────────────────────────────────────────────────────
inline double qct_timestamp_to_unix(uint64_t ts) {
    double secs_since_gps = static_cast<double>(ts) / 32768.0;
    return secs_since_gps + 315964800.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds-checked read helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace diag_util {

    inline bool check_bounds(const uint8_t* buf, size_t buf_len,
                             size_t offset, size_t need) {
        return (offset + need) <= buf_len;
    }

    template<typename T>
    inline bool read_le(const uint8_t* buf, size_t buf_len,
                        size_t offset, T& out) {
        if (!check_bounds(buf, buf_len, offset, sizeof(T))) return false;
        memcpy(&out, buf + offset, sizeof(T));
        return true;
    }

    template<typename T>
    inline const T* as_struct(const uint8_t* buf, size_t buf_len, size_t offset = 0) {
        if (!check_bounds(buf, buf_len, offset, sizeof(T))) return nullptr;
        return reinterpret_cast<const T*>(buf + offset);
    }

    // ── Hex dump for debugging raw DIAG payloads ────────────────────────────
    inline void hex_dump(const char* tag, const uint8_t* data, size_t len, size_t max_bytes = 64) {
        // Fixed-size buffer (no VLA) — enough for 256-byte dump @ 3 chars each + "+...(N)"
        constexpr size_t BUF_SIZE = 256 * 3 + 32;
        char buf[BUF_SIZE];
        size_t show = (len < max_bytes) ? len : max_bytes;
        if (show > 256) show = 256;  // hard cap to fit in BUF_SIZE
        size_t pos = 0;
        for (size_t i = 0; i < show && pos < BUF_SIZE - 4; ++i) {
            pos += snprintf(buf + pos, BUF_SIZE - pos, "%02X ", data[i]);
        }
        if (show < len) {
            snprintf(buf + pos, BUF_SIZE - pos, "...(+%zu)", len - show);
        }
        DIAG_LOGD("%s [%zu bytes]: %s", tag, len, buf);
    }

    // ── Read uint32/uint16 from unaligned LE buffer ─────────────────────────
    inline uint32_t rd32(const uint8_t* p) {
        uint32_t v; memcpy(&v, p, 4); return v;
    }
    inline uint16_t rd16(const uint8_t* p) {
        uint16_t v; memcpy(&v, p, 2); return v;
    }

    // ── Extract bitfield: 'nbits' starting at 'lsb' from a uint32 ──────────
    inline uint32_t bits(uint32_t word, int lsb, int nbits) {
        return (word >> lsb) & ((1u << nbits) - 1u);
    }

    // ── Sign-extend a value of 'nbits' width ───────────────────────────────
    inline int32_t sign_extend(uint32_t val, int nbits) {
        uint32_t sign_bit = 1u << (nbits - 1);
        return static_cast<int32_t>((val ^ sign_bit) - sign_bit);
    }

    // ── Qualcomm ML1 measurement conversions ────────────────────────────────
    // These are the standard formulas used by QXDM / SCAT / MobileInsight:
    //   RSRP (dBm) = raw * 0.0625 - 180.0
    //   RSRQ (dB)  = raw * 0.0625 -  30.0
    //   RSSI (dBm) = raw * 0.0625 - 110.0
    //
    // Raw values are unsigned integers (typically 10-12 bits).

    inline int16_t ml1_rsrp(uint32_t raw) {
        double v = raw * 0.0625 - 180.0;
        return static_cast<int16_t>(v < 0 ? v - 0.5 : v + 0.5);
    }
    inline int16_t ml1_rsrq(uint32_t raw) {
        double v = raw * 0.0625 - 30.0;
        return static_cast<int16_t>(v < 0 ? v - 0.5 : v + 0.5);
    }
    inline int16_t ml1_rssi(uint32_t raw) {
        double v = raw * 0.0625 - 110.0;
        return static_cast<int16_t>(v < 0 ? v - 0.5 : v + 0.5);
    }

    // ── Sanity checks for extracted values ──────────────────────────────────
    // 3GPP TS 36.101 Table 5.7.3-1: max valid LTE EARFCN is 70645 (Band 88).
    // Values above this (e.g. 245733 seen in some firmware glitches) are
    // garbage — typically high bytes of a 5G NR-ARFCN bleeding through.
    inline bool valid_lte_earfcn(uint32_t v)  { return v > 0 && v <= 70645; }
    inline bool valid_lte_pci(uint16_t v)     { return v <= 503; }
    inline bool valid_lte_rsrp(int16_t v)     { return v >= -180 && v <= -30; }
    inline bool valid_nr_arfcn(uint32_t v)    { return v <= 3279165; }
    inline bool valid_nr_pci(uint16_t v)      { return v <= 1007; }

} // namespace diag_util

#endif // DIAG_COMMON_H
