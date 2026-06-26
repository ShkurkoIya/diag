// lte_rrc_ota_decoder.h
//
// Wrapper parser for Qualcomm DIAG log code 0xB0C0 — "LTE RRC OTA Packet".
//
// What this decodes:
//   The Qualcomm-proprietary header that wraps every BCCH/PCCH/CCCH/DCCH
//   ASN.1 PDU coming off the air. Out of this header we get:
//     - PCI (physical cell ID)
//     - EARFCN
//     - SFN/sub-frame
//     - PDU type (1=BCCH-DL-SCH, 2=BCCH-BCH, 6=PCCH, ...)
//     - PDU length
//   Then the rest of the packet is the raw ASN.1 PER octet stream that
//   we hand off to lte_sib1_decoder for SIB1, or to other decoders for
//   other PDU types.
//
// Why this is layered:
//   The 0xB0C0 wrapper has been redesigned by Qualcomm something like 25
//   times since LTE shipped. Each modem firmware uses a different `version`
//   byte (0x02..0x23+ at last count). The byte ORDER and FIELD LAYOUT
//   change between versions — EARFCN went from u16 to u32 around v0x09,
//   an NR-RRC release byte was inserted around v0x14, etc.
//
//   Rather than hard-code one layout and break on every new firmware, we
//   keep a TABLE of versions and their field offsets. Unknown versions
//   are reported with the version byte and raw hex so the user can send
//   them back and we can add support.
//
// Sources for the offset tables (cross-checked):
//   - scat/signalcat: src/scat/parsers/qualcomm/diaglteproto.py
//   - mobile_sentinel: parsers/qualcomm/diagltelogparser.py
//   - osmocom diag-parser: src/protocol/lte_rrc.c
//   - SnoopSnitch: gsm-parser/diag_lte.c

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lte_rrc {

    /**
 * Channel/PDU type within the LTE RRC OTA packet. The on-the-wire byte
 * has been remapped at least twice in Qualcomm history; the canonical
 * decoded form is what scat/wireshark settled on:
 *
 *   1 = BCCH_BCH        (MIB)
 *   2 = BCCH_DL_SCH     (SIB1 + SystemInformation)
 *   3 = MCCH
 *   4 = PCCH            (Paging)
 *   5 = DL_CCCH         (RRC Connection Setup, etc)
 *   6 = DL_DCCH         (dedicated DL)
 *   7 = UL_CCCH
 *   8 = UL_DCCH
 *
 * The wrapper parser remaps the firmware-specific byte to this canonical
 * enum. SIB1 lives in BCCH_DL_SCH.
 */
    enum class PduType : uint8_t {
        UNKNOWN = 0,
        BCCH_BCH = 1,
        BCCH_DL_SCH = 2,
        MCCH = 3,
        PCCH = 4,
        DL_CCCH = 5,
        DL_DCCH = 6,
        UL_CCCH = 7,
        UL_DCCH = 8,
    };

    /**
 * Result of parsing the 0xB0C0 wrapper.
 *
 * `pdu` and `pdu_len` point INTO the original buffer — caller must keep
 * the buffer alive while using these. They reference the start of the
 * raw ASN.1 PER PDU that should be fed to lte_sib1_decoder etc.
 */
    struct LteRrcOta {
        /** Wrapper version byte (0x02..0x23 known so far). */
        uint8_t version = 0;
        /** RRC release × 10 (e.g. 90 = R9, 130 = R13). 0 if unknown. */
        uint8_t rrc_release = 0;
        /** NR RRC release × 10 if present (added in v≥0x14). 0 otherwise. */
        uint8_t nr_rrc_release = 0;
        /** Radio bearer ID (0..31), or 0 if SIB. */
        uint8_t rb_id = 0;
        /** Physical Cell ID (0..503). */
        uint16_t pci = 0;
        /** EARFCN. */
        uint32_t earfcn = 0;
        /** SFN. */
        uint16_t sfn = 0;
        /** Sub-frame number (0..9). */
        uint8_t sub_fn = 0;
        /** Channel/PDU type (BCCH-DL-SCH = 2 for SIB1). */
        PduType pdu_type = PduType::UNKNOWN;
        /** Raw u8 PDU type byte as stored on the wire (firmware-specific). */
        uint8_t pdu_type_raw = 0;
        /** Pointer into source buffer to the start of the ASN.1 PDU. */
        const uint8_t *pdu = nullptr;
        /** Length of the ASN.1 PDU. */
        uint16_t pdu_len = 0;

        /** Did we recognise the version and parse cleanly? */
        bool ok = false;
        /** Human-readable error if !ok. */
        std::string error;
    };

    /**
 * Parse the body of a 0xB0C0 LTE RRC OTA log packet.
 *
 * @param buf  Pointer to bytes AFTER the 12-byte LogRecord header — i.e.
 *             starting at the wrapper version byte.
 * @param len  Length of those bytes.
 *
 * If the version is recognised, returns a LteRrcOta with .ok=true and
 * .pdu/.pdu_len pointing into `buf`. If unknown, .ok=false and .error
 * describes which version it was. Caller can dump the raw bytes to a log
 * for later analysis and we can add the version to the table.
 *
 * Even on .ok=false, .version is filled in (it's just the first byte).
 */
    LteRrcOta decode_ota_wrapper(const uint8_t *buf, size_t len);

    /**
 * Diagnostic: human-readable description of a wrapper version byte.
 * Returns "v0x09 (R9, B/H/H/B/H/L/H/B/L/H)" style string for known
 * versions, "v0xXX (UNSUPPORTED)" for unknown.
 */
    std::string describe_version(uint8_t version);

}// namespace lte_rrc