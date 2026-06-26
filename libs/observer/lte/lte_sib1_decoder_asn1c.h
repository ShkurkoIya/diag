// lte_sib1_decoder_asn1c.h
//
// asn1c-based decoder for LTE RRC SIB1, drop-in replacement for the
// hand-written lte_sib1_decoder.h.
//
// Same struct types and entry-point function names — the only thing
// that changes for callers is the #include line. This way you can
// switch between hand-written and asn1c-generated implementations by
// changing one line in lte_lte_log_parser.cpp:
//
//   #include "lte_sib1_decoder.h"           // hand-written
// vs
//   #include "lte_sib1_decoder_asn1c.h"     // asn1c-generated
//
// Why have both? The hand-written one is 250 LOC, zero deps, easy to
// debug. The asn1c one supports ALL of SIB1 (extensions, Rel-15+
// fields, etc.) and SIB2..SIB17, but adds ~2 MB to the binary. Use
// asn1c when you actually need the extra coverage.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lte_rrc {

    // These structs are deliberately IDENTICAL to the ones in
    // lte_sib1_decoder.h. Reusing the names means the rest of the codebase
    // (parse_rrc_ota in diag_lte_log_parser.cpp) doesn't need to change at
    // all when switching backends.

    struct PlmnIdentity {
        std::string mcc;
        std::string mnc;
        bool cell_reserved_for_operator_use = false;
        std::string mccmnc() const { return mcc + mnc; }
    };

    struct Sib1Decoded {
        std::vector<PlmnIdentity> plmn_list;
        uint16_t tac = 0;
        uint32_t cell_id = 0;
        uint16_t freq_band = 0;
        bool cell_barred = false;
        bool intra_freq_reselection_allowed = true;
        bool csg_indication = false;
        std::optional<uint32_t> csg_identity;
        bool ok = false;
        std::string error;
    };

    struct MibDecoded {
        uint8_t dl_bandwidth_rb = 0;
        uint16_t sfn_msb8 = 0;
        uint8_t phich_duration = 0;
        uint8_t phich_resource = 0;
        bool ok = false;
    };

    /**
 * Decode a SIB1 from its raw ASN.1 PER octet stream using asn1c-generated
 * decoder. Same input/output as the hand-written version.
 *
 * Internally:
 *   1. Calls uper_decode_complete() with asn_DEF_BCCH_DL_SCH_Message
 *   2. Walks the resulting tree, picks out SIB1 fields we care about
 *   3. Frees the asn1c structure with ASN_STRUCT_FREE
 *
 * Performance: ~10x slower than the hand-written decoder (allocates
 * ~50 small heap objects per call vs zero allocations). Still fast
 * enough — sub-millisecond per packet on Snapdragon.
 */
    Sib1Decoded decode_sib1(const uint8_t *bcch_dl_sch_pdu, size_t len);

    /** Decode an MIB. */
    MibDecoded decode_mib(const uint8_t *bcch_bch_pdu, size_t len);

}// namespace lte_rrc