// asn1c_lte_bridge.h
//
// Bridge between our DIAG pipeline and asn1c-generated LTE RRC parsers.
//
// Mirrors reference project's parse4G() pattern but returns strongly-typed
// DecodeResult instead of printing to FILE*.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace lte_asn1 {

    enum class PduKind {
        UNKNOWN,
        MIB,       // BCCH-BCH-Message
        SIB1,      // BCCH-DL-SCH → SystemInformationBlockType1
        SI_MESSAGE,// BCCH-DL-SCH → SystemInformation (SIB2..)
        PAGING,
        DL_CCCH,
        DL_DCCH,
        UL_CCCH,
        UL_DCCH,
        DECODE_FAILED,
    };

    struct PlmnEntry {
        std::string mcc;
        std::string mnc;
        bool reserved_for_operator_use = false;
        std::string mccmnc() const { return mcc + mnc; }
    };

    struct Sib1Fields {
        std::vector<PlmnEntry> plmn_list;
        uint16_t tac = 0;
        uint32_t cell_id = 0;
        uint16_t freq_band = 0;
        bool cell_barred = false;
        bool intra_freq_reselection_allowed = true;
        bool csg_indication = false;
        std::optional<uint32_t> csg_identity;
    };

    struct MibFields {
        uint8_t dl_bandwidth_rb = 0;
        uint16_t sfn_msb8 = 0;
        uint8_t phich_duration = 0;
        uint8_t phich_resource = 0;
    };

    // One cell's measurement from a UL-DCCH MeasurementReport. The serving
    // (PCell) entry has is_serving=true and pci=-1 (its PCI is taken from the
    // OTA packet header instead). Neighbor entries carry their physCellId.
    // rsrp_dbm / rsrq_db are 0 when the field was absent in the report.
    struct MeasResultEntry {
        int pci = -1;
        int rsrp_dbm = 0;
        int rsrq_db = 0;
        bool is_serving = false;
    };

    // A neighbor cell's PCI→EARFCN mapping extracted from SIB4 (intra-freq,
    // earfcn=0 meaning "serving frequency") or SIB5 (inter-freq, earfcn set).
    // Used to resolve the carrier frequency of cells seen in MeasurementReports
    // and ML1 measurements, which only carry PCI.
    struct NeighborFreq {
        int pci = -1;
        uint32_t earfcn = 0;// 0 = intra-freq (same as serving)
        bool inter_freq = false;
    };

    struct DecodeResult {
        PduKind kind = PduKind::UNKNOWN;
        bool ok = false;
        std::string error;
        std::string message_type;// e.g. "SIB1", "SI [sib2,sib3]", "MeasurementReport"
        std::optional<Sib1Fields> sib1;
        std::optional<MibFields> mib;
        std::vector<MeasResultEntry> meas_results;// populated for UL-DCCH MeasurementReport
        std::vector<NeighborFreq> neighbor_freqs; // populated for SIB4/SIB5 (SystemInformation)
        std::vector<uint8_t> raw_dump;
    };

    DecodeResult decode_pdu(const uint8_t *pdu, size_t pdu_len, const char *pdu_name);

    int print_pdu(const uint8_t *pdu, size_t pdu_len, const char *pdu_name, FILE *fp);

    // Full decoded structure as text (asn_fprint tree), captured into a string.
    std::string decode_pdu_text(const uint8_t *pdu, size_t pdu_len, const char *pdu_name);

    const char *pdu_type_name(uint8_t pdu_type_canonical);

    // ── Diagnostic helpers ────────────────────────────────────────────────
    /** Number of entries in asn_pdu_collection[]. 0 means pdu_collection.c
 *  not properly linked (check ASN_PDU_COLLECTION=1 define). */
    int pdu_collection_size();

    /** Print all PDU names from asn_pdu_collection[] to fp (or stderr). */
    void list_pdu_names(FILE *fp);

    /** Encode-decode roundtrip selftest with synthetic SIB1.
 *  Returns true if asn1c integration is fully working. */
    bool selftest();

}// namespace lte_asn1