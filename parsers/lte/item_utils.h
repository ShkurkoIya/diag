// item_utils.h
//
// Parsing helpers for Qualcomm DIAG LTE log packets.
//
// References:
//   - scat parse_lte_rrc_cell_info: src/scat/parsers/qualcomm/diagltelogparser.py
//   - scat issue #66: confirms 0xB0C2 v2/v3 carries MCC/MNC/TAC/Band
//     output: "EARFCN 6400/24400, Band 20, BW 10/10 MHz, PCI 100,
//              xTAC/xCID 63a1/1b6430d, MCC 262, MNC 01"
//
// NOTE: MCC, MNC, MNC_digit are stored as DECIMAL INTEGERS (not BCD).
// Example: MCC=250 stored as bytes 0xFA 0x00 (little-endian u16).
// mnc_digit indicates the count of digits in MNC (2 or 3).

#ifndef OBSERVER_ITEM_UTILS_H
#define OBSERVER_ITEM_UTILS_H

#include <cstdint>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────
// Little-endian readers
// ─────────────────────────────────────────────────────────────────────────
static inline uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

static inline uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// ─────────────────────────────────────────────────────────────────────────
// 0xB0C0 RRC OTA — wrapper item layout for version 25/26 (SM8550 era)
//
// pkt_body[1:21] reading (skip 1-byte version stored separately):
//   p[0..3]   rrc_rel_maj/min/nr_rrc_rel_maj/min
//   p[4]      rb_id
//   p[5..6]   PCI (u16 LE)
//   p[7..10]  EARFCN (u32 LE)
//   p[11..12] SFN+SubFN packed (u16 LE: SFN<<5 | SubFn for some versions)
//   p[13]     PDU type (zero-indexed for v25+: 0=BCCH_BCH, 1=BCCH_DL_SCH, ...)
//   p[14..17] SIB mask (or carrier index)
//   p[18..19] PDU length (u16 LE)
//   p[20..]   ASN.1 PER PDU
// ─────────────────────────────────────────────────────────────────────────
struct ItemV25 {
    uint8_t  rrc_rel_maj;
    uint8_t  rrc_rel_min;
    uint8_t  nr_rrc_rel_maj;
    uint8_t  nr_rrc_rel_min;
    uint8_t  rbid;
    uint16_t pci;
    uint32_t earfcn;
    uint16_t sfn_subfn;
    uint8_t  pdu_num;
    uint32_t sib_mask;
    uint16_t len;
};

static bool parse_item_v25(const uint8_t* pkt_body, size_t pkt_len, ItemV25& out) {
    if (pkt_len < 21) return false;

    const uint8_t* p = pkt_body + 1;

    out.rrc_rel_maj    = p[0];
    out.rrc_rel_min    = p[1];
    out.nr_rrc_rel_maj = p[2];
    out.nr_rrc_rel_min = p[3];
    out.rbid           = p[4];
    out.pci            = read_le16(p + 5);
    out.earfcn         = read_le32(p + 7);
    out.sfn_subfn      = read_le16(p + 11);
    out.pdu_num        = p[13];
    out.sib_mask       = read_le32(p + 14);
    out.len            = read_le16(p + 18);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// 0xB0C2 LTE RRC Serving Cell Info — VERSION 2
//
// Payload after 1-byte version (pkt_body[1:25] reading = 24 bytes):
//   p[0..1]   PCI            (u16 LE)
//   p[2..3]   DL EARFCN      (u16 LE)        ← 16-bit in v2
//   p[4..5]   UL EARFCN      (u16 LE)        ← 16-bit in v2
//   p[6]      DL Bandwidth   (u8, in RBs/6: 0=1.4MHz, ... 5=20MHz)
//   p[7]      UL Bandwidth   (u8)
//   p[8..11]  Cell ID        (u32 LE — 28 bits in lower bits)
//   p[12..13] TAC            (u16 LE)
//   p[14..17] Band           (u32 LE — 3GPP band number, 1..71+)
//   p[18..19] MCC            (u16 LE, DECIMAL value e.g. 250)
//   p[20]     MNC digit count (u8, 2 or 3)
//   p[21..22] MNC            (u16 LE, DECIMAL value)
//   p[23]     allowed_access (u8, 0=normal, 1=restricted, etc)
// ─────────────────────────────────────────────────────────────────────────
struct QcDiagLteRrcServCellInfo_v2 {
    uint16_t pci;
    uint16_t dl_earfcn;
    uint16_t ul_earfcn;
    uint8_t  dl_bw;
    uint8_t  ul_bw;
    uint32_t cell_id;
    uint16_t tac;
    uint32_t band;
    uint16_t mcc;
    uint8_t  mnc_digit_count;
    uint16_t mnc;
    uint8_t  allowed_access;
};

static bool parse_qcdiag_lte_rrc_serv_cellinfo_v2(const uint8_t* pkt_body,
                                                  size_t pkt_len,
                                                  QcDiagLteRrcServCellInfo_v2& out) {
    if (pkt_len < 25) return false;   // 1 (ver) + 24 (payload)

    const uint8_t* p = pkt_body + 1;

    out.pci             = read_le16(p);           // ← FIXED: was p[0]
    out.dl_earfcn       = read_le16(p + 2);
    out.ul_earfcn       = read_le16(p + 4);
    out.dl_bw           = p[6];
    out.ul_bw           = p[7];
    out.cell_id         = read_le32(p + 8);
    out.tac             = read_le16(p + 12);
    out.band            = read_le32(p + 14);
    out.mcc             = read_le16(p + 18);
    out.mnc_digit_count = p[20];
    out.mnc             = read_le16(p + 21);
    out.allowed_access  = p[23];

    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// 0xB0C2 LTE RRC Serving Cell Info — VERSION 3
//
// Same as v2 but DL/UL EARFCN widened to u32 (needed for >65535 EARFCNs,
// which started with LTE band 32+ around 2014).
//
// Payload after 1-byte version (pkt_body[1:29] reading = 28 bytes):
//   p[0..1]   PCI            (u16 LE)
//   p[2..5]   DL EARFCN      (u32 LE)        ← 32-bit in v3
//   p[6..9]   UL EARFCN      (u32 LE)        ← 32-bit in v3
//   p[10]     DL Bandwidth   (u8)
//   p[11]     UL Bandwidth   (u8)
//   p[12..15] Cell ID        (u32 LE)
//   p[16..17] TAC            (u16 LE)
//   p[18..21] Band           (u32 LE)
//   p[22..23] MCC            (u16 LE)
//   p[24]     MNC digit count (u8)
//   p[25..26] MNC            (u16 LE)
//   p[27]     allowed_access (u8)
// ─────────────────────────────────────────────────────────────────────────
struct QcDiagLteRrcServCellInfo_v3 {
    uint16_t pci;
    uint32_t dl_earfcn;
    uint32_t ul_earfcn;
    uint8_t  dl_bw;
    uint8_t  ul_bw;
    uint32_t cell_id;
    uint16_t tac;
    uint32_t band;
    uint16_t mcc;
    uint8_t  mnc_digit_count;
    uint16_t mnc;
    uint8_t  allowed_access;
};

static bool parse_qcdiag_lte_rrc_serv_cellinfo_v3(const uint8_t* pkt_body,
                                                  size_t pkt_len,
                                                  QcDiagLteRrcServCellInfo_v3& out) {
    if (pkt_len < 29) return false;   // 1 (ver) + 28 (payload)

    const uint8_t* p = pkt_body + 1;

    out.pci             = read_le16(p);           // ← FIXED: was p[0]
    out.dl_earfcn       = read_le32(p + 2);
    out.ul_earfcn       = read_le32(p + 6);
    out.dl_bw           = p[10];
    out.ul_bw           = p[11];
    out.cell_id         = read_le32(p + 12);
    out.tac             = read_le16(p + 16);
    out.band            = read_le32(p + 18);
    out.mcc             = read_le16(p + 22);
    out.mnc_digit_count = p[24];
    out.mnc             = read_le16(p + 25);
    out.allowed_access  = p[27];

    return true;
}

struct QcDiagLteMib_v1 {
    uint16_t  pci;
    uint16_t earfcn;
    uint16_t sfn;
    uint8_t tx_antenna;
    uint8_t bandwidth;
};

static bool parse_qcdiag_lte_mib_v1(const uint8_t* pkt_body,
                                    size_t pkt_len,
                                    QcDiagLteMib_v1& out) {
    if (pkt_len < 9) return false;   // 0 (ver) + 9 (payload)

    const uint8_t* p = pkt_body + 1;

    out.pci             = read_le16(p);           // ← FIXED: was p[0]
    out.earfcn          = read_le16(p + 2);
    out.sfn             = read_le16(p + 4);
    out.tx_antenna      = p[6];
    out.bandwidth      = p[7];
    return true;
}

struct QcDiagLteMib_v2 {
    uint16_t  pci;
    uint32_t earfcn;
    uint16_t sfn;
    uint8_t tx_antenna;
    uint8_t bandwidth;
};

static bool parse_qcdiag_lte_mib_v2(const uint8_t* pkt_body,
                                    size_t pkt_len,
                                    QcDiagLteMib_v2& out) {
    if (pkt_len < 11) return false;   // 0 (ver) + 11 (payload)

    const uint8_t* p = pkt_body + 1;

    out.pci             = read_le16(p);           // ← FIXED: was p[0]
    out.earfcn          = read_le32(p + 2);
    out.sfn             = read_le16(p + 6);
    out.tx_antenna      = p[8];
    out.bandwidth      = p[9];
    return true;
}

struct QcDiagLteMib_v17 {
    uint16_t  pci;
    uint32_t earfcn;
    uint16_t sfn;
    uint8_t sfn_msb4;
    uint8_t hsfn_lsb2;
    uint8_t sib1_sch_info;
    uint8_t si_value_tag;
    uint8_t access_barring;
    uint8_t opmode_type;
    uint16_t opmode_info;
    uint8_t tx_antenna;
};

static bool parse_qcdiag_lte_mib_v17(const uint8_t* pkt_body,
                                    size_t pkt_len,
                                    QcDiagLteMib_v17& out) {
    if (pkt_len < 16) return false;   // 0 (ver) + 11 (payload)

    const uint8_t* p = pkt_body + 1;

    out.pci             = read_le16(p);           // ← FIXED: was p[0]
    out.earfcn          = read_le32(p + 2);
    out.sfn             = read_le16(p + 6);
    out.sfn_msb4        = p[8];
    out.hsfn_lsb2       = p[9];
    out.sib1_sch_info   = p[10];
    out.si_value_tag    = p[11];
    out.access_barring  = p[12];
    out.opmode_type     = read_le16(p+13);
    out.tx_antenna      = p[15];
    return true;
}
#endif //OBSERVER_ITEM_UTILS_H