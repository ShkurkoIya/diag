// lte_sib1_decoder.cpp
//
// PER (UNALIGNED) decoder for LTE RRC SIB1.
//
// Bit-level layout reference: 3GPP TS 36.331 §6.3 EUTRA-RRC-Definitions.asn
// The relevant excerpt for SIB1:
//
//   SystemInformationBlockType1 ::= SEQUENCE {
//       cellAccessRelatedInfo SEQUENCE {
//           plmn-IdentityList         PLMN-IdentityList,
//           trackingAreaCode          BIT STRING (SIZE (16)),
//           cellIdentity              BIT STRING (SIZE (28)),
//           cellBarred                ENUMERATED {barred, notBarred},
//           intraFreqReselection      ENUMERATED {allowed, notAllowed},
//           csg-Indication            BOOLEAN,
//           csg-Identity              BIT STRING (SIZE (27)) OPTIONAL
//       },
//       cellSelectionInfo SEQUENCE { ... },
//       p-Max INTEGER (-30..33) OPTIONAL,
//       freqBandIndicator INTEGER (1..64),     -- extended to 1..256 in r10
//       schedulingInfoList SchedulingInfoList,
//       ... (lots more) ...
//   }
//
//   PLMN-IdentityList ::= SEQUENCE (SIZE (1..6)) OF PLMN-IdentityInfo
//   PLMN-IdentityInfo ::= SEQUENCE {
//       plmn-Identity                  PLMN-Identity,
//       cellReservedForOperatorUse     ENUMERATED {reserved, notReserved}
//   }
//   PLMN-Identity ::= SEQUENCE {
//       mcc                            MCC OPTIONAL,
//       mnc                            MNC
//   }
//   MCC ::= SEQUENCE (SIZE (3)) OF MCC-MNC-Digit
//   MNC ::= SEQUENCE (SIZE (2..3)) OF MCC-MNC-Digit
//   MCC-MNC-Digit ::= INTEGER (0..9)

#include "lte_sib1_decoder.h"

#include <cstring>

namespace lte_rrc {

    namespace {

        // ─────────────────────────────────────────────────────────────────────────
        // Bit reader — reads MSB-first from a byte buffer. PER UNALIGNED reads
        // fields at arbitrary bit offsets without padding to byte boundaries.
        //
        // On overrun, all subsequent reads return 0 and `eof` is set. Callers
        // that care about correctness should check `eof()` after a sequence of
        // reads or check return values.
        // ─────────────────────────────────────────────────────────────────────────
        class BitReader {
        public:
            BitReader(const uint8_t *data, size_t len)
                : data_(data), bit_count_(len * 8), pos_(0), eof_(false) {}

            /** Read up to 32 bits as an unsigned integer, MSB-first. */
            uint32_t read(unsigned nbits) {
                if (nbits == 0) return 0;
                if (nbits > 32) {
                    eof_ = true;
                    return 0;
                }
                if (pos_ + nbits > bit_count_) {
                    eof_ = true;
                    return 0;
                }
                uint32_t v = 0;
                for (unsigned i = 0; i < nbits; ++i) {
                    v <<= 1;
                    v |= bit_at(pos_ + i);
                }
                pos_ += nbits;
                return v;
            }

            /** Read a single bit as a bool. */
            bool read_bool() { return read(1) != 0; }

            /** Skip n bits forward. Used for unknown extension fragments. */
            void skip(unsigned nbits) {
                if (pos_ + nbits > bit_count_) {
                    eof_ = true;
                    pos_ = bit_count_;
                } else
                    pos_ += nbits;
            }

            /**
             * Read N "octets" as raw bytes, but at the current (possibly mid-byte)
             * bit offset. Returns nbytes bytes assembled bit-by-bit.
             */
            void read_octets(uint8_t *out, size_t nbytes) {
                for (size_t i = 0; i < nbytes; ++i) {
                    out[i] = static_cast<uint8_t>(read(8));
                }
            }

            bool eof() const { return eof_; }
            size_t pos() const { return pos_; }
            size_t remaining() const { return bit_count_ > pos_ ? bit_count_ - pos_ : 0; }

        private:
            /** Bit at absolute position p (0 = MSB of first byte). */
            unsigned bit_at(size_t p) const {
                size_t byte = p >> 3;
                size_t bit = 7 - (p & 7);
                return (data_[byte] >> bit) & 1u;
            }

            const uint8_t *data_;
            size_t bit_count_;
            size_t pos_;
            bool eof_;
        };

        /**
 * Decode a CONSTRAINED INTEGER per X.691 §10.5.6 (UNALIGNED variant).
 * For small ranges (lb..ub fits in a few bits), it's just N raw bits where
 * N = ceil(log2(ub - lb + 1)).
 */
        uint32_t per_constrained_int(BitReader &r, uint32_t lb, uint32_t ub) {
            if (ub <= lb) return lb;
            uint32_t range = ub - lb + 1;
            unsigned nbits = 0;
            while ((1u << nbits) < range) ++nbits;
            uint32_t v = r.read(nbits);
            return lb + v;
        }

        /**
 * Encode 3 BCD digits (each is INTEGER 0..9 = 4 bits raw in PER) into a
 * 3-character ASCII string. Used for both MCC (always 3 digits) and MNC
 * (2 or 3 digits).
 */
        std::string read_digit_string(BitReader &r, unsigned ndigits) {
            std::string out;
            out.reserve(ndigits);
            for (unsigned i = 0; i < ndigits; ++i) {
                uint32_t d = per_constrained_int(r, 0, 9);
                out.push_back(static_cast<char>('0' + (d & 0xF)));
            }
            return out;
        }

    }// anonymous namespace

    // ─────────────────────────────────────────────────────────────────────────
    // SIB1 decoder
    // ─────────────────────────────────────────────────────────────────────────

    Sib1Decoded decode_sib1(const uint8_t *pdu, size_t len) {
        Sib1Decoded out;
        if (!pdu || len < 2) {
            out.error = "PDU too short";
            return out;
        }
        BitReader r(pdu, len);

        // ── BCCH-DL-SCH-Message ::= SEQUENCE { message CHOICE { c1 ..., msgClassExtension ... } }
        //   The CHOICE has 2 alternatives, so 1-bit selector. c1 = 0.
        // ── c1 itself is a CHOICE between 4 SI types: systemInformation,
        //   systemInformationBlockType1, etc. 2-bit selector.
        //   Per 36.331, sib1 is index 1 in c1 (0=systemInformation, 1=sib1,
        //   2=spare-7, 3=spare-6 in current rel — extension marker first).
        //
        // BUT — Qualcomm's 0xB0C0 log already strips the BCCH-DL-SCH-Message
        // wrapper for SIB1 packets in some versions. We try to handle both:
        // first try with wrapper, fall back to bare SIB1 parsing if MCC turns
        // out to be insane.
        //
        // For robustness we check the first byte: if top bit is 0 (CHOICE
        // index 0 = c1), we have the wrapper. Otherwise bare SIB1.
        bool has_wrapper = (pdu[0] & 0x80) == 0;

        if (has_wrapper) {
            // Skip BCCH-DL-SCH outer CHOICE: 1 bit (must be 0 = c1)
            r.read_bool();

            // c1 CHOICE: 2 bits — we want value 1 (sib1)
            uint32_t c1_alt = r.read(2);
            if (c1_alt != 1) {
                // Could be other SI message — 0=systemInformation containing
                // SIB2..N. We only handle SIB1 here.
                out.error = "not SIB1 (c1 alt=" + std::to_string(c1_alt) + ")";
                return out;
            }
        }

        // ── SystemInformationBlockType1 ::= SEQUENCE { ... } ──
        // Has extension marker (top of SEQUENCE). First bit = "extension present?"
        (void) r.read_bool();// extension marker — we don't decode extensions

        // Optional/default field bitmap.
        // SIB1 has these OPTIONAL fields in the root group:
        //   - p-Max OPTIONAL
        //   - tdd-Config OPTIONAL
        //   - nonCriticalExtension OPTIONAL
        // 3 bits.
        bool has_p_max = r.read_bool();
        (void) r.read_bool();// has_tdd_config — we don't decode TDD config
        (void) r.read_bool();// has_non_critical_ext — we don't decode extensions

        // ── cellAccessRelatedInfo SEQUENCE ──
        //  No extension marker on this inner SEQUENCE in r8 baseline — but
        //  later releases added one. We try to detect: if the value would
        //  exceed plmn list length, we're misaligned.
        //
        //  Inner OPTIONALs:
        //    csg-Identity OPTIONAL  (1 bit)
        bool has_csg_identity = r.read_bool();

        // PLMN-IdentityList ::= SEQUENCE (SIZE (1..6)) OF PLMN-IdentityInfo
        //   length encoded as INTEGER (1..6) → 3 bits, but PER encodes
        //   constrained range 1..6 needing ceil(log2(6))=3 bits, value = real-1
        uint32_t plmn_count = per_constrained_int(r, 1, 6);
        if (plmn_count > 6) {
            out.error = "plmn count too big (" + std::to_string(plmn_count) + ")";
            return out;
        }

        for (uint32_t i = 0; i < plmn_count; ++i) {
            // PLMN-IdentityInfo ::= SEQUENCE { plmn-Identity, cellReservedForOperatorUse }
            //   No extension marker.
            // PLMN-Identity ::= SEQUENCE { mcc OPTIONAL, mnc }
            //   1-bit OPTIONAL bitmap for mcc.
            bool has_mcc = r.read_bool();

            PlmnIdentity p;
            if (has_mcc) {
                // MCC ::= SEQUENCE (SIZE (3)) OF MCC-MNC-Digit (INTEGER 0..9)
                //   Fixed size 3 → no length encoded.
                p.mcc = read_digit_string(r, 3);
            }
            // MNC ::= SEQUENCE (SIZE (2..3)) OF MCC-MNC-Digit
            //   Length encoded: 1 bit (0=2 digits, 1=3 digits) since range is 2..3
            bool mnc_3digit = r.read_bool();
            unsigned mnc_len = mnc_3digit ? 3 : 2;
            p.mnc = read_digit_string(r, mnc_len);

            // cellReservedForOperatorUse ENUMERATED {reserved, notReserved} = 1 bit
            p.cell_reserved_for_operator_use = (r.read(1) == 0);

            out.plmn_list.push_back(std::move(p));
        }

        // If MCC was omitted on a non-first entry, fill from the previous one
        // (3GPP allows omitting MCC if same as previous PLMN's MCC).
        for (size_t i = 1; i < out.plmn_list.size(); ++i) {
            if (out.plmn_list[i].mcc.empty()) {
                out.plmn_list[i].mcc = out.plmn_list[i - 1].mcc;
            }
        }

        // ── trackingAreaCode BIT STRING (SIZE (16)) ──
        out.tac = static_cast<uint16_t>(r.read(16));

        // ── cellIdentity BIT STRING (SIZE (28)) ──
        out.cell_id = r.read(28);

        // ── cellBarred ENUMERATED {barred, notBarred} = 1 bit ──
        //   value 0 = barred, 1 = notBarred
        out.cell_barred = (r.read(1) == 0);

        // ── intraFreqReselection ENUMERATED {allowed, notAllowed} = 1 bit ──
        //   value 0 = allowed, 1 = notAllowed
        out.intra_freq_reselection_allowed = (r.read(1) == 0);

        // ── csg-Indication BOOLEAN = 1 bit ──
        out.csg_indication = r.read_bool();

        // ── csg-Identity BIT STRING (SIZE (27)) OPTIONAL ──
        if (has_csg_identity) {
            out.csg_identity = r.read(27);
        }

        // ── cellSelectionInfo SEQUENCE { q-RxLevMin, q-RxLevMinOffset OPTIONAL } ──
        //   We don't need this for cell discovery — just skip past.
        //   It has 1 OPTIONAL bit + INTEGER (-70..-22) = 6 bits + maybe (1..8) = 3 bits
        bool has_q_rxlevmin_offset = r.read_bool();
        r.skip(6);                           // q-RxLevMin
        if (has_q_rxlevmin_offset) r.skip(3);// q-RxLevMinOffset

        // ── p-Max INTEGER (-30..33) OPTIONAL ──
        if (has_p_max) {
            // range 64 → 6 bits
            r.skip(6);
        }

        // ── freqBandIndicator INTEGER (1..64) ── (or 1..256 in extension)
        //   Base: 6 bits. We'll trust the base range in r8 SIB1 — extension
        //   for >64 came later and is encoded differently.
        out.freq_band = static_cast<uint16_t>(per_constrained_int(r, 1, 64));

        // We've extracted everything we care about. Don't bother continuing
        // through schedulingInfoList, tdd-Config, si-WindowLength, etc.

        out.ok = !r.eof();
        return out;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MIB decoder — fixed 24-bit format
    //
    //   MasterInformationBlock ::= SEQUENCE {
    //       dl-Bandwidth ENUMERATED {n6, n15, n25, n50, n75, n100}, -- 3 bits
    //       phich-Config SEQUENCE {
    //           phich-Duration ENUMERATED {normal, extended},        -- 1 bit
    //           phich-Resource ENUMERATED {oneSixth, half, one, two} -- 2 bits
    //       },
    //       systemFrameNumber BIT STRING (SIZE (8)),                 -- 8 bits
    //       schedulingInfoSIB1-BR-r13 INTEGER (0..31) OPTIONAL,
    //       systemInfoUnchanged-BR-r15 BOOLEAN OPTIONAL,
    //       spare BIT STRING                                          -- pad to 24
    //   }
    //
    // First 14 useful bits, total fixed 24 bits in PER.
    // ─────────────────────────────────────────────────────────────────────────

    MibDecoded decode_mib(const uint8_t *pdu, size_t len) {
        MibDecoded out;
        if (!pdu || len < 3) return out;
        BitReader r(pdu, len);

        // Extension marker bit (in r8 was absent; later releases added it).
        // Some Qualcomm logs include the BCCH-BCH-Message wrapper, others bare MIB.
        // Cheap heuristic: if first 3 bits = 0..5 (a valid bandwidth enum), bare;
        // otherwise assume wrapper-prefixed. We'll just try bare first.
        uint8_t bw_idx = static_cast<uint8_t>(r.read(3));
        static const uint8_t bw_to_rb[6] = {6, 15, 25, 50, 75, 100};
        if (bw_idx < 6) {
            out.dl_bandwidth_rb = bw_to_rb[bw_idx];
        }
        out.phich_duration = static_cast<uint8_t>(r.read(1));
        out.phich_resource = static_cast<uint8_t>(r.read(2));
        out.sfn_msb8 = static_cast<uint16_t>(r.read(8));

        out.ok = (out.dl_bandwidth_rb != 0);
        return out;
    }

}// namespace lte_rrc