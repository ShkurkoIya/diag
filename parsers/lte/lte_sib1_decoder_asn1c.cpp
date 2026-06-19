// lte_sib1_decoder_asn1c.cpp
//
// This file is currently a stub. The asn1c-based SIB1 decoding is
// implemented in asn1c_lte_bridge.cpp. This file is kept as a
// placeholder for future direct asn1c-backed SIB1/MIB decoders if
// needed (per the design described in lte_sib1_decoder_asn1c.h).
//
// The .h file declares decode_sib1() / decode_mib() for callers that
// prefer this API over the asn1c_lte_bridge::decode_pdu() generic
// interface. If/when those callers appear, implement the declared
// functions here using asn1c_lte_bridge internally.

#include "lte_sib1_decoder_asn1c.h"

// (No implementations yet — callers route through asn1c_lte_bridge.)
