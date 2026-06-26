#pragma once
#ifndef GSM_CELL_METRICS_H
#define GSM_CELL_METRICS_H
//
// gsm_cell_metrics.h — GSM band classification + C1/C2 cell-(re)selection
// criteria (3GPP TS 45.008 §6.4) + SI-3/SI-4 Cell Selection Parameter parsing.
//
// Header-only and dependency-free so it can be unit-tested off-device.
// NCC/BCC are not here — they are a trivial split of BSIC (NCC=bsic>>3,
// BCC=bsic&7) done at the call site.
//
#include <cstddef>
#include <cstdint>

namespace gsm_metrics {

    // ─────────────────────────────────────────────────────────────────────────────
    // Band classification (3GPP TS 45.005 §2). ARFCN ranges overlap between
    // DCS1800 and PCS1900 (both 512–810), so an optional band-class hint (the
    // Qualcomm arfcn_band>>12 nibble) disambiguates; default is DCS1800 (the
    // European/RU case — MTS).
    // ─────────────────────────────────────────────────────────────────────────────
    enum class GsmBand : uint8_t { UNKNOWN = 0,
                                   GSM850,
                                   GSM900,
                                   DCS1800,
                                   PCS1900,
                                   GSM450,
                                   GSM480 };

    inline GsmBand gsm_band(uint16_t arfcn, int band_hint = -1) {
        if (arfcn == 0 || (arfcn >= 1 && arfcn <= 124) || (arfcn >= 975 && arfcn <= 1023))
            return GsmBand::GSM900;// P-GSM + E-GSM900
        if (arfcn >= 128 && arfcn <= 251) return GsmBand::GSM850;
        if (arfcn >= 259 && arfcn <= 293) return GsmBand::GSM450;
        if (arfcn >= 306 && arfcn <= 340) return GsmBand::GSM480;
        if (arfcn >= 512 && arfcn <= 885) {
            // Overlap region. PCS1900 only meaningful in band-class hint == PCS.
            if (band_hint == 1 /*PCS per QC band class*/) return GsmBand::PCS1900;
            return GsmBand::DCS1800;
        }
        return GsmBand::UNKNOWN;
    }

    inline const char *gsm_band_name(GsmBand b) {
        switch (b) {
            case GsmBand::GSM850:
                return "GSM850";
            case GsmBand::GSM900:
                return "GSM900";
            case GsmBand::DCS1800:
                return "DCS1800";
            case GsmBand::PCS1900:
                return "PCS1900";
            case GsmBand::GSM450:
                return "GSM450";
            case GsmBand::GSM480:
                return "GSM480";
            default:
                return "";
        }
    }
    inline const char *gsm_band_name(uint16_t arfcn, int band_hint = -1) {
        return gsm_band_name(gsm_band(arfcn, band_hint));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // MS max RF output power P (dBm) — typical handset class per band (TS 45.005).
    // Used only for the B term of C1; documented assumption, see compute_c1().
    // ─────────────────────────────────────────────────────────────────────────────
    inline int ms_max_power_dbm(GsmBand b) {
        switch (b) {
            case GsmBand::DCS1800:
            case GsmBand::PCS1900:
                return 30;// class 1 (1 W)
            default:
                return 33;// GSM850/900 class 4 (2 W)
        }
    }

    // MS_TXPWR_MAX_CCH power-control level → dBm (TS 45.005 §4.1.1).
    inline int pwr_ctrl_to_dbm(uint8_t level, GsmBand b) {
        if (b == GsmBand::DCS1800 || b == GsmBand::PCS1900) {
            if (level == 29) return 36;
            if (level == 30) return 34;
            if (level == 31) return 32;
            if (level <= 28) return 30 - 2 * static_cast<int>(level);
            return 30;
        }
        // GSM850/900/450/480
        if (level <= 2) return 39;
        if (level <= 19) return 39 - 2 * (static_cast<int>(level) - 2);
        return 5;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // C1 (TS 45.008 §6.4):  C1 = A − max(B, 0)
    //   A = RXLEV_average(units 0..63) − RXLEV_ACCESS_MIN(units 0..63)
    //   B = MS_TXPWR_MAX_CCH(dBm) − P(dBm)
    // rxlev_dbm is the measured serving/neighbour level (e.g. −85). We convert it
    // to the 0..63 RXLEV unit scale (RXLEV = clamp(dBm + 110, 0, 63)) so A is a dB
    // value, matching the spec. In practice MS_TXPWR_MAX_CCH ≤ P so B ≤ 0 → C1 = A.
    // ─────────────────────────────────────────────────────────────────────────────
    inline int compute_c1(int rxlev_dbm, uint8_t rxlev_access_min,
                          uint8_t ms_txpwr_max_cch, GsmBand band) {
        int rxlev_units = rxlev_dbm + 110;
        if (rxlev_units < 0) rxlev_units = 0;
        if (rxlev_units > 63) rxlev_units = 63;

        int A = rxlev_units - static_cast<int>(rxlev_access_min);
        int B = pwr_ctrl_to_dbm(ms_txpwr_max_cch, band) - ms_max_power_dbm(band);
        if (B < 0) B = 0;
        return A - B;
    }

    // C2 (TS 45.008 §6.4). When reselection parameters are present:
    //   PENALTY_TIME != 31:  C2 = C1 + CELL_RESELECT_OFFSET − TEMPORARY_OFFSET·H(PENALTY_TIME − T)
    //   PENALTY_TIME == 31:  C2 = C1 − CELL_RESELECT_OFFSET
    // For a passive snapshot the per-cell timer T is unknown; we report the steady
    // state (T > PENALTY_TIME ⇒ H()=0), i.e. C2 = C1 + CRO. Offsets are in dB:
    // CELL_RESELECT_OFFSET step = 2 dB, TEMPORARY_OFFSET step = 10 dB (7 = infinity).
    inline int compute_c2(int c1, bool params_present,
                          uint8_t cell_reselect_offset, uint8_t temporary_offset,
                          uint8_t penalty_time) {
        if (!params_present) return c1;
        int cro = static_cast<int>(cell_reselect_offset) * 2;// dB
        if (penalty_time == 31) return c1 - cro;
        (void) temporary_offset;// H()=0 in steady state
        return c1 + cro;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // MSB-first bit reader for CSN.1 rest octets.
    // ─────────────────────────────────────────────────────────────────────────────
    struct BitReader {
        const uint8_t *p;
        size_t nbits;
        size_t pos = 0;
        BitReader(const uint8_t *buf, size_t nbytes) : p(buf), nbits(nbytes * 8) {}
        bool eof(size_t need = 1) const { return pos + need > nbits; }
        uint32_t get(size_t n) {
            uint32_t v = 0;
            for (size_t i = 0; i < n; ++i) {
                uint32_t bit = 0;
                if (pos < nbits) bit = (p[pos >> 3] >> (7 - (pos & 7))) & 1u;
                v = (v << 1) | bit;
                ++pos;
            }
            return v;
        }
    };

    // Parsed Cell Selection / reselection parameters for one cell.
    struct CellSelParams {
        bool valid = false;           // access params (for C1) found
        uint8_t rxlev_access_min = 0; // 0..63
        uint8_t ms_txpwr_max_cch = 0; // 0..31 power-control level
        bool resel_present = false;   // reselection params (for C2) found
        uint8_t cell_reselect_off = 0;// 0..63
        uint8_t temporary_offset = 0; // 0..7
        uint8_t penalty_time = 0;     // 0..31
    };

    // Parse the 2-octet Cell Selection Parameters IE (TS 44.018 §10.5.2.4):
    //   octet1: CELL-RESELECT-HYSTERESIS[3] MS-TXPWR-MAX-CCH[5]
    //   octet2: ACS[1] NECI[1] RXLEV-ACCESS-MIN[6]
    inline void parse_cell_selection_params(const uint8_t *o, CellSelParams &out) {
        out.ms_txpwr_max_cch = o[0] & 0x1F;
        out.rxlev_access_min = o[1] & 0x3F;
        out.valid = true;
    }

    // Parse SI-3 Rest Octets "Optional Selection Parameters" (TS 44.018 §10.5.2.34).
    // rest points at the first rest-octet; it begins with a 1-bit presence flag for
    // the Selection Parameters struct: { 0 | 1 CBQ(1) CRO(6) TO(3) PT(5) }.
    inline void parse_si3_rest_octets(const uint8_t *rest, size_t rest_len,
                                      CellSelParams &out) {
        if (rest_len == 0) return;
        BitReader br(rest, rest_len);
        if (br.get(1) == 1) {// Selection Parameters present
            br.get(1);       // CBQ
            out.cell_reselect_off = static_cast<uint8_t>(br.get(6));
            out.temporary_offset = static_cast<uint8_t>(br.get(3));
            out.penalty_time = static_cast<uint8_t>(br.get(5));
            out.resel_present = true;
        }
    }

}// namespace gsm_metrics
#endif// GSM_CELL_METRICS_H
