// ─────────────────────────────────────────────────────────────────────────────
// umts_rrc_solib.cpp — UMTS/WCDMA RRC (25.331) decoder, built into a SEPARATE
// shared object libdiag_umts_rrc.so. asn1c emits fixed global symbols
// (asn_pdu_collection[], the runtime, per-type asn_DEF_*). LTE (36.331) and
// UMTS (25.331) both define BCCH-BCH-Message, DL-DCCH-Message, etc., so linking
// both asn1c libs into one binary collapses them (LTE wins) and UMTS payloads
// decode against the LTE grammar — the "0x412F = LTE MIB" garbage. Building the
// UMTS decoder into its own .so with hidden visibility, exporting only the one
// entry below, keeps the two grammars in separate symbol namespaces.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
#include <asn_application.h>
#include <asn_internal.h>
#include <constr_TYPE.h>
extern asn_TYPE_descriptor_t* asn_pdu_collection[];
}

#define UMTS_EXPORT __attribute__((visibility("default")))

namespace {
const char* chan_hint_pdu(int chan_type) {
    switch (chan_type & 0x07) {
        case 0: return "UL-CCCH-Message";
        case 1: return "UL-DCCH-Message";
        case 2: return "DL-CCCH-Message";
        case 3: return "DL-DCCH-Message";
        case 4: return "BCCH-BCH-Message";
        case 5: return "BCCH-FACH-Message";
        case 6: return "PCCH-Message";
        default: return nullptr;
    }
}
asn_TYPE_descriptor_t* find_pdu(const char* name) {
    for (int i = 0; asn_pdu_collection[i]; ++i)
        if (std::strcmp(asn_pdu_collection[i]->name, name) == 0)
            return asn_pdu_collection[i];
    return nullptr;
}
bool try_one(const uint8_t* pdu, size_t len, const char* name, char* out, size_t out_cap) {
    asn_TYPE_descriptor_t* t = find_pdu(name);
    if (!t) return false;
    void* st = nullptr;
    asn_dec_rval_t rv = asn_decode(nullptr, ATS_UNALIGNED_BASIC_PER, t, &st, pdu, len);
    bool ok = (rv.code == RC_OK && rv.consumed > 0);
    if (ok && out && out_cap) {
        char* buf = nullptr; size_t sz = 0;
        FILE* ms = open_memstream(&buf, &sz);
        if (ms) { asn_fprint(ms, t, st); std::fclose(ms); }
        if (buf) { std::snprintf(out, out_cap, "%.*s", (int)sz, buf); std::free(buf); }
    }
    if (st) ASN_STRUCT_FREE(*t, st);
    return ok;
}
}  // namespace

extern "C" UMTS_EXPORT
int umts_rrc_decode_any(const uint8_t* pdu, int len, int chan_hint,
                        char* out, int out_cap, char* matched, int matched_cap) {
    if (matched && matched_cap) matched[0] = '\0';
    if (!pdu || len <= 0) {
        if (out && out_cap) std::snprintf(out, out_cap, "[umts] empty payload");
        return 0;
    }
    const char* order[] = {
        chan_hint_pdu(chan_hint),
        "BCCH-BCH-Message", "DL-DCCH-Message", "DL-CCCH-Message",
        "UL-DCCH-Message", "UL-CCCH-Message", "BCCH-FACH-Message", "PCCH-Message",
    };
    for (const char* name : order) {
        if (!name) continue;
        if (try_one(pdu, (size_t)len, name, out, (size_t)out_cap)) {
            if (matched && matched_cap) std::snprintf(matched, matched_cap, "%s", name);
            return 1;
        }
    }
    if (out && out_cap)
        std::snprintf(out, out_cap, "[umts] no candidate 25.331 PDU decoded this payload");
    return 0;
}
