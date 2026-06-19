#pragma once
// asn1c_umts_bridge.h — UMTS/WCDMA RRC (25.331) decoder bridge.
// The UMTS asn1c grammar shares C symbol names with the LTE grammar, so the
// UMTS decoder lives in a SEPARATE shared object (libdiag_umts_rrc.so) reached
// via dlopen — the main binary never links the UMTS asn1c symbols, so they
// cannot clash with the LTE asn1c it carries. If the .so is absent the decode_*
// functions return a short diagnostic instead of a wrong LTE-grammar decode.
#include <cstdint>
#include <cstddef>
#include <string>

namespace umts_asn1 {
    const char* chan_to_pdu_name(int chan_type);
    std::string decode_pdu_text_any(const uint8_t* pdu, size_t pdu_len,
                                    int chan_hint, std::string& matched_pdu);
    std::string decode_pdu_text(const uint8_t* pdu, size_t pdu_len, const char* pdu_name);
}  // namespace umts_asn1
