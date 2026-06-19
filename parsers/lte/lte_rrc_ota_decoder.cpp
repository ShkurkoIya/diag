// lte_rrc_ota_decoder.cpp
//
// Wrapper parser for 0xB0C0. See header for top-level rationale.
//
// ─────────────────────────────────────────────────────────────────────────
// Wrapper format reference table — collated from scat / mobile_sentinel /
// diag-parser / SnoopSnitch.
//
// All wrappers START with a 1-byte version. Then come the fixed-layout
// fields. The end is always:
//   ... pdu_type_raw (u8) ... pdu_len (u16 LE) ... pdu_bytes ...
//
// Field order varies — usually one of these shapes:
//   BHHBHLHBLH  (v0x08, 0x09, 0x0c, 0x0d, 0x0f, 0x10, 0x13, 0x14, 0x16):
//     version:1 rrc_rel:1 nr_rel:1 rbid:1 pci:2 earfcn:4 sfn_sf:2 type:1 len:2
//   BHBHLHBLH   (older, v0x02..0x07, no nr_rel):
//     version:1 rrc_rel:1 rbid:1 pci:2 earfcn:4 sfn_sf:2 type:1 len:2
//   BHBHHHBLH   (v0x04..0x07, u16 EARFCN):
//     version:1 rrc_rel:1 rbid:1 pci:2 earfcn:2 sfn_sf:2 type:1 len:2
//
// Newer Snapdragon (865+ era, v0x18..0x23) added more fields between rb_id
// and pci, often a sysinfo carrier index. The exact layout for every
// >=0x18 version isn't fully documented in any single open-source project —
// we cover what's known and treat the rest as "unsupported, dump raw".
//
// PDU type byte semantics also CHANGED between versions. We have two
// remap tables (legacy vs modern) and pick based on version range.
// ─────────────────────────────────────────────────────────────────────────

#include "lte_rrc_ota_decoder.h"

#include <cstring>

namespace lte_rrc {

    namespace {

// ─── Helpers ─────────────────────────────────────────────────────────────

        inline uint16_t rd_u16le(const uint8_t* p) {
            return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
        }

        inline uint32_t rd_u32le(const uint8_t* p) {
            return static_cast<uint32_t>(p[0])
                   | (static_cast<uint32_t>(p[1]) << 8)
                   | (static_cast<uint32_t>(p[2]) << 16)
                   | (static_cast<uint32_t>(p[3]) << 24);
        }

// ─── PDU type remap tables ──────────────────────────────────────────────
//
// Different firmware versions encode "what kind of message is this PDU"
// differently. Here are the two well-known mappings:
//
// Legacy mapping (versions <= 0x07):
//   1 = BCCH_BCH, 2 = BCCH_DL_SCH, 3 = MCCH, 4 = PCCH,
//   5 = DL_CCCH, 6 = DL_DCCH, 7 = UL_CCCH, 8 = UL_DCCH
// Modern mapping (versions >= 0x09): same numbering, BUT some firmwares
//   use 0x21 = BCCH_DL_SCH and 0x22 = BCCH_BCH etc. (offset-by-0x20).
//
// Practically: we use the canonical numbering when the raw byte fits
// 1..8, otherwise we mask with 0x1F and try again, otherwise UNKNOWN.

        PduType remap_pdu_type(uint8_t raw, uint8_t /*version*/) {
            uint8_t v = raw;
            if (v > 8 && v <= 0x28) v &= 0x1F;  // strip 0x20-bit some firmwares add
            switch (v) {
                case 1: return PduType::BCCH_BCH;
                case 2: return PduType::BCCH_DL_SCH;
                case 3: return PduType::MCCH;
                case 4: return PduType::PCCH;
                case 5: return PduType::DL_CCCH;
                case 6: return PduType::DL_DCCH;
                case 7: return PduType::UL_CCCH;
                case 8: return PduType::UL_DCCH;
                default: return PduType::UNKNOWN;
            }
        }

// ─── Layout descriptors ─────────────────────────────────────────────────
//
// We describe each known wrapper layout as a small struct giving the
// offsets where each field lives, plus the total header size before
// the PDU bytes start. This is the same approach scat takes (but with
// struct.pack format strings — same idea).

        struct Layout {
            bool     valid       = false;
            bool     has_nr_rel  = false;
            uint8_t  off_rrc_rel;  // offset of rrc_release byte
            uint8_t  off_nr_rel;   // offset of nr_rrc_release (only if has_nr_rel)
            uint8_t  off_rb_id;
            uint8_t  off_pci;      // u16 LE
            uint8_t  off_earfcn;   // u16 LE or u32 LE
            bool     earfcn_is_u32;
            uint8_t  off_sfn_sf;   // u16 LE: SFN<<4 | sub-frame  (some fw use SFN<<5)
            bool     sfn_shift_5;  // true: SFN encoded as bits 15..5 (sub-frame in 4..0)
            uint8_t  off_pdu_type; // u8
            uint8_t  off_pdu_len;  // u16 LE
            uint8_t  header_size;  // total header = off_pdu_len + 2
            /** Some Snapdragon 8 Gen 2+ wrappers have +6 extra bytes between sfn_sf
             *  and pdu_type. We don't know what they encode (carrier-aggregation
             *  context?) but we don't need them for cell discovery either. */
            uint8_t  pdu_type_remap_offset = 0;  // add to raw type byte before remap
        };

// Build a "BHHBHLHBLH" style layout (modern shape with nr_rel).
        constexpr Layout layout_with_nr_rel(bool sfn_shift5 = false) {
            return Layout{
                    /*valid*/      true,
                    /*has_nr_rel*/ true,
                    /*off_rrc_rel*/ 1,
                    /*off_nr_rel*/  2,
                    /*off_rb_id*/   3,
                    /*off_pci*/     4,
                    /*off_earfcn*/  6,
                    /*earfcn_u32*/  true,
                    /*off_sfn_sf*/  10,
                    /*sfn_shift5*/  sfn_shift5,
                    /*off_pdu_type*/ 12,
                    /*off_pdu_len*/  13,
                    /*header_size*/  15,
                    /*type_remap*/  0,
            };
        }

// Older shape without nr_rrc_release byte.
        constexpr Layout layout_no_nr_rel(bool earfcn_u32, bool sfn_shift5 = false) {
            if (earfcn_u32) {
                return Layout{
                        true, false,
                        1,    // rrc_rel
                        0,    // n/a
                        2,    // rb_id
                        3,    // pci u16
                        5,    // earfcn u32
                        true,
                        9,    // sfn_sf u16
                        sfn_shift5,
                        11,   // pdu_type u8
                        12,   // pdu_len u16
                        14,
                        0,    // type_remap
                };
            } else {
                return Layout{
                        true, false,
                        1, 0,
                        2,    // rb_id
                        3,    // pci u16
                        5,    // earfcn u16
                        false,
                        7,    // sfn_sf u16
                        sfn_shift5,
                        9,    // pdu_type u8
                        10,   // pdu_len u16
                        12,
                        0,    // type_remap
                };
            }
        }

// ─── v0x1B Layout (Snapdragon 8 Gen 2 / SM8550) ─────────────────────────
//
// REVERSE-ENGINEERED from a real packet captured on Xiaomi 13T (SM8550).
// Sample (LTE band 7, EARFCN 3200, PCI 130, real cell from MTS Russia):
//   1B 10 10 10 90 00 82 00 80 0C 00 00 B9 E4 07 00 00 00 00 07 00 ...
//   ^^ ^^ ^^ ^^ ^^^^^ ^^^^^ ^^^^^^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^ ^^^^^
//   |  |  |  |  |     |     |           |     |     |     |  pdu_len = 7
//   |  |  |  |  |     |     |           |     |     |     pdu_type = 0
//   |  |  |  |  |     |     |           |     |     unknown_3
//   |  |  |  |  |     |     |           |     unknown_2
//   |  |  |  |  |     |     |           SFN_SF = 0xE4B9 (SFN<<4 → SFN=3659 SF=9)
//   |  |  |  |  |     |     EARFCN u32 LE = 3200
//   |  |  |  |  |     PCI u16 LE = 130
//   |  |  |  |  unknown_1 (CA carrier index? sysinfo bitmap?)
//   |  |  |  rb_id = 0x10
//   |  |  nr_rrc_release = R16
//   |  rrc_release = R16
//   version = 0x1B
//
//   Header total = 21 bytes, then PDU (7 bytes for this sample).
//
// Note: pdu_type encoding is DIFFERENT from older versions. v0x1B uses
// ZERO-BASED indexing (we observed 0x00 for what should be BCCH-DL-SCH).
// Older versions used 1-based: 1=BCCH-BCH, 2=BCCH-DL-SCH. We compensate
// via pdu_type_remap_offset = +1.
        constexpr Layout layout_sm8550_v1b() {
            return Layout{
                    /*valid*/      true,
                    /*has_nr_rel*/ true,
                    /*off_rrc_rel*/  1,
                    /*off_nr_rel*/   2,
                    /*off_rb_id*/    3,
                    /*off_pci*/      6,
                    /*off_earfcn*/   8,
                    /*earfcn_u32*/   true,
                    /*off_sfn_sf*/   12,
                    /*sfn_shift5*/   false,   // v0x1B uses SFN<<4 (verified: SF=9 ≤ 9)
                    /*off_pdu_type*/ 18,
                    /*off_pdu_len*/  19,
                    /*header_size*/  21,
                    /*type_remap*/   1,        // v0x1B is 0-indexed → add 1 before mapping
            };
        }

/**
 * Look up the layout for a given version byte.
 *
 * Coverage strategy: most LTE-only modems fall in v0x02..0x16. NR-NSA
 * Snapdragon (845 era) added v0x14..0x16 with nr_rel byte. NR-SA modems
 * (X55+ era, Snapdragon 865+ / 8 Gen 1+) bumped to v0x18..0x23 with
 * additional CA carrier-index field — for those we mark UNSUPPORTED
 * and let the caller dump raw hex so we can add real samples.
 */
        Layout layout_for_version(uint8_t v) {
            switch (v) {
                // ── Pre-NR (legacy, no nr_rrc_release byte) ──
                case 0x02:
                case 0x03:
                    return layout_no_nr_rel(/*earfcn_u32=*/false);
                case 0x04:
                case 0x06:
                case 0x07:
                case 0x08:  // some firmwares: still no nr_rel here
                    return layout_no_nr_rel(/*earfcn_u32=*/false);

                    // ── EARFCN widened to u32 (Cat-4+ devices, ~2014+) ──
                case 0x0B:
                    return layout_no_nr_rel(/*earfcn_u32=*/true);

                    // ── NR_RRC_Release byte added (NR-NSA capable, ~Snapdragon 845+) ──
                case 0x09:
                case 0x0C:
                case 0x0D:
                case 0x0E:
                case 0x0F:
                case 0x10:
                case 0x13:
                case 0x14:
                case 0x16:
                    return layout_with_nr_rel(/*sfn_shift5=*/false);

                    // ── SFN shift changed to 5 bits in some R15 firmwares ──
                case 0x18:
                case 0x19:
                case 0x1A:
                    return layout_with_nr_rel(/*sfn_shift5=*/true);

                    // ── v0x1B: SM8550 (Snapdragon 8 Gen 2) — REVERSE-ENGINEERED ──
                    // 21-byte header, EARFCN at offset 8 (u32), PCI at 6, pdu_type
                    // at 18 with +1 remap offset (zero-indexed). See layout_sm8550_v1b()
                    // for the full annotated breakdown.
                case 0x1B:
                    return layout_sm8550_v1b();

                    // ── Newer Snapdragon (865+ / 8 Gen 1 / 8 Gen 2 / 8 Gen 3) ──
                    // Other versions in this range — best-effort with the v0x1B
                    // layout as starting point. Will produce diagnostic output if
                    // it fails the sanity check.
                case 0x1C:
                case 0x1D:
                case 0x1E:
                case 0x1F:
                case 0x20:
                case 0x21:
                case 0x22:
                case 0x23:
                    return layout_sm8550_v1b();

                default:
                    return Layout{};  // .valid=false
            }
        }

    }  // anonymous namespace

    LteRrcOta decode_ota_wrapper(const uint8_t* buf, size_t len) {
        LteRrcOta out;
        if (!buf || len < 1) {
            out.error = "empty buffer";
            return out;
        }
        out.version = buf[0];

        Layout L = layout_for_version(out.version);
        if (!L.valid) {
            out.error = "unsupported version 0x" + std::string{} +
                        "0123456789ABCDEF"[out.version >> 4] +
                        "0123456789ABCDEF"[out.version & 0xF];
            return out;
        }
        if (len < L.header_size) {
            out.error = "buffer shorter than header";
            return out;
        }

        out.rrc_release = buf[L.off_rrc_rel];
        if (L.has_nr_rel) out.nr_rrc_release = buf[L.off_nr_rel];
        out.rb_id  = buf[L.off_rb_id];
        out.pci    = rd_u16le(&buf[L.off_pci]);
        out.earfcn = L.earfcn_is_u32
                     ? rd_u32le(&buf[L.off_earfcn])
                     : rd_u16le(&buf[L.off_earfcn]);
        uint16_t sfn_sf = rd_u16le(&buf[L.off_sfn_sf]);
        if (L.sfn_shift_5) {
            out.sfn    = static_cast<uint16_t>(sfn_sf >> 5);
            out.sub_fn = static_cast<uint8_t>(sfn_sf & 0x1F);
        } else {
            out.sfn    = static_cast<uint16_t>(sfn_sf >> 4);
            out.sub_fn = static_cast<uint8_t>(sfn_sf & 0xF);
        }
        out.pdu_type_raw = buf[L.off_pdu_type];
        // Some firmware uses zero-based PDU type indexing (v0x1B+).
        // Add the layout's remap offset before applying the canonical mapping.
        uint8_t mapped_raw = static_cast<uint8_t>(out.pdu_type_raw + L.pdu_type_remap_offset);
        out.pdu_type     = remap_pdu_type(mapped_raw, out.version);
        out.pdu_len      = rd_u16le(&buf[L.off_pdu_len]);

        // Sanity check the PDU bounds.
        if (static_cast<size_t>(L.header_size) + out.pdu_len > len) {
            out.error = "PDU length exceeds buffer";
            return out;
        }

        // Sanity check the parsed values for a SIB1 packet — if they're
        // wildly out of range, our layout guess for this version is wrong.
        // PCI is 0..503, EARFCN is 0..262143 in current 3GPP.
        if (out.pci > 1007 || out.earfcn > 0x10000000u) {
            out.error = "values out of range — layout mismatch likely";
            return out;
        }

        out.pdu = buf + L.header_size;
        out.ok  = true;
        return out;
    }

    std::string describe_version(uint8_t version) {
        Layout L = layout_for_version(version);
        char vbuf[8];
        vbuf[0] = 'v'; vbuf[1] = '0'; vbuf[2] = 'x';
        vbuf[3] = "0123456789ABCDEF"[version >> 4];
        vbuf[4] = "0123456789ABCDEF"[version & 0xF];
        vbuf[5] = '\0';
        std::string s(vbuf);
        if (!L.valid) {
            s += " (UNSUPPORTED)";
            return s;
        }
        s += " (";
        s += L.has_nr_rel ? "NR-aware, " : "legacy, ";
        s += L.earfcn_is_u32 ? "u32 EARFCN, " : "u16 EARFCN, ";
        s += L.sfn_shift_5 ? "SFN<<5" : "SFN<<4";
        s += ")";
        return s;
    }

}  // namespace lte_rrc