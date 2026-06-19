// asn1c_umts_bridge.cpp — thin shim reaching the isolated UMTS RRC decoder in
// libdiag_umts_rrc.so via dlopen/dlsym. No asn1c symbols referenced from the
// main binary, so the LTE asn1c grammar cannot be shadowed. See header.
#include "asn1c_umts_bridge.h"
#include <dlfcn.h>
#include <unistd.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace umts_asn1 {

const char* chan_to_pdu_name(int chan_type) {
    switch (chan_type) {
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

using DecodeFn = int (*)(const uint8_t*, int, int, char*, int, char*, int);

static DecodeFn resolve_decoder() {
    static DecodeFn fn = [] () -> DecodeFn {
        void* h = ::dlopen("libdiag_umts_rrc.so", RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            char exe[PATH_MAX] = {0};
            ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n > 0) {
                exe[n] = '\0';
                char* slash = std::strrchr(exe, '/');
                if (slash) {
                    *slash = '\0';
                    std::string p = std::string(exe) + "/libdiag_umts_rrc.so";
                    h = ::dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
                }
            }
        }
        if (!h) return nullptr;
        return reinterpret_cast<DecodeFn>(::dlsym(h, "umts_rrc_decode_any"));
    }();
    return fn;
}

std::string decode_pdu_text_any(const uint8_t* pdu, size_t pdu_len,
                                int chan_hint, std::string& matched_pdu) {
    matched_pdu.clear();
    if (!pdu || pdu_len == 0) return std::string();
    DecodeFn fn = resolve_decoder();
    if (!fn) {
        return std::string("[umts] libdiag_umts_rrc.so not loaded — UMTS 25.331 "
                           "decoder unavailable (build it with USE_ASN1C_UMTS and "
                           "ship it alongside the scan binary)");
    }
    std::vector<char> out(64 * 1024);
    char mbuf[64] = {0};
    int rc = fn(pdu, (int)pdu_len, chan_hint, out.data(), (int)out.size(), mbuf, sizeof(mbuf));
    if (rc == 1) matched_pdu = mbuf;
    return std::string(out.data());
}

std::string decode_pdu_text(const uint8_t* pdu, size_t pdu_len, const char* pdu_name) {
    int hint = -1;
    for (int c = 0; c <= 6; ++c) {
        const char* n = chan_to_pdu_name(c);
        if (n && pdu_name && std::strcmp(n, pdu_name) == 0) { hint = c; break; }
    }
    std::string matched;
    return decode_pdu_text_any(pdu, pdu_len, hint, matched);
}

}  // namespace umts_asn1
