// lte_sib1_decoder.h
//
// Standalone ASN.1 PER (Packed Encoding Rules, UNALIGNED) decoder for the
// LTE RRC SIB1 fields we care about for cell discovery:
//
//   - PLMN-IdentityList (up to 6 entries): MCC + MNC of each operator
//     broadcasting on this cell.
//   - trackingAreaCode  (16-bit TAC)
//   - cellIdentity      (28-bit eNB+CellID combined)
//   - freqBandIndicator (1..256, the LTE band number)
//   - cellBarred / intraFreqReselection / csg-Indication flags
//
// We deliberately do NOT pull in asn1c, srsRAN's asn1 lib, or anything else.
// SIB1 is a small fixed-shape message; bit-banging it directly is ~300 lines
// of code with full comments, vs ~1.5 MB binary if we link srsRAN.
//
// References:
//   3GPP TS 36.331 §6.2.2 SystemInformationBlockType1
//   3GPP TS 36.331 §6.3   ASN.1 module EUTRA-RRC-Definitions
//   ITU-T X.691           Packed Encoding Rules (PER) — UNALIGNED variant
//
// PER UNALIGNED is what LTE RRC uses everywhere. Key encoding rules we
// implement here (a tiny subset of X.691):
//   - SEQUENCE: bit per OPTIONAL/DEFAULT field (presence map), then fields
//   - INTEGER (CONSTRAINED): N bits where N = ceil(log2(ub-lb+1))
//   - SEQUENCE OF (SIZE constrained): N-bit length + items
//   - CHOICE: index in N bits where N = ceil(log2(num_alts))
//   - ENUMERATED: index in N bits where N = ceil(log2(num_values))
//   - BIT STRING (SIZE fixed): N bits raw
//
// What we do NOT implement (because SIB1 doesn't use it):
//   - extension marker handling beyond skip-and-continue
//   - aligned PER
//   - large-fragmented OCTET STRING
//   - real numbers
//
// If a future SIB version adds extensions we need, the bit-reader is here
// and adding more decoders is straightforward.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lte_rrc {

    /** One PLMN entry from the SIB1 plmn-IdentityList. */
    struct PlmnIdentity {
        /** 3-digit MCC, e.g. "250" for Russia. */
        std::string mcc;
        /** 2-digit or 3-digit MNC, e.g. "01" for MTS RUS, "099" for Beeline. */
        std::string mnc;
        /** True iff cell is reserved for operator use only (UE-class IoT etc). */
        bool cell_reserved_for_operator_use = false;

        /** Combined 5- or 6-char numeric form, e.g. "25001". */
        std::string mccmnc() const { return mcc + mnc; }
    };

    /** Result of decoding a SIB1 message. */
    struct Sib1Decoded {
        /** All PLMNs broadcasting on this cell (most LTE: 1, but MOCN can have 6). */
        std::vector<PlmnIdentity> plmn_list;

        /** Tracking Area Code, 16 bits. */
        uint16_t tac = 0;

        /** Cell Identity, 28 bits (combines eNB-id + sector). */
        uint32_t cell_id = 0;

        /** LTE band number (1..256). */
        uint16_t freq_band = 0;

        bool cell_barred = false;
        bool intra_freq_reselection_allowed = true;
        bool csg_indication = false;

        /** Optional CSG-Identity (27-bit) if present. */
        std::optional<uint32_t> csg_identity;

        /** Did decode succeed? If false, only some fields may be valid. */
        bool ok = false;
        /** Human-readable error if decode failed (empty on success). */
        std::string error;
    };

    /** Result of decoding an MIB (24 bits, fixed format). */
    struct MibDecoded {
        /** Downlink bandwidth in resource blocks: 6, 15, 25, 50, 75, or 100. */
        uint8_t dl_bandwidth_rb = 0;
        /** Most-significant 8 bits of System Frame Number. */
        uint16_t sfn_msb8 = 0;
        /** PHICH config — typically not interesting for cell discovery. */
        uint8_t phich_duration = 0;// 0 = normal, 1 = extended
        uint8_t phich_resource = 0;// 0=oneSixth, 1=half, 2=one, 3=two
        bool ok = false;
    };

    /**
 * Decode a SIB1 from its raw ASN.1 PER octet stream.
 *
 * Input is the body of a BCCH-DL-SCH-Message containing a SIB1, exactly as
 * transmitted on the air (and exactly as Qualcomm's DIAG log code 0xB0C0
 * delivers it after we strip the small log-packet wrapper).
 *
 * The function is lenient — any unknown extensions are skipped and we
 * keep going. Returns Sib1Decoded with .ok=true on success.
 */
    Sib1Decoded decode_sib1(const uint8_t *bcch_dl_sch_pdu, size_t len);

    /**
 * Decode an MIB from BCH (Broadcast Channel). MIB is exactly 3 bytes
 * (24 bits) of ASN.1 PER. Used for the dl_bandwidth field.
 */
    MibDecoded decode_mib(const uint8_t *bcch_bch_pdu, size_t len);

}// namespace lte_rrc