#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// journal.h — decoupled packet-journal sink.
//
// Parsers (LTE RRC, NR, UMTS…) and the raw-frame path push JournalRecord's here;
// main.cpp registers a sink that serialises them to the --journal NDJSON file.
// Header-only API + a tiny journal.cpp holding the sink storage, so any parser
// can emit without depending on main.cpp.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

struct JournalRecord {
    double      t        = 0.0;        // unix seconds (fractional)
    uint16_t    code     = 0;          // diag log code
    const char* rat      = "UNKNOWN";  // RAT string
    std::string channel;               // logical channel, e.g. "BCCH-DL-SCH", "PCCH"
    std::string msg_type;              // decoded message, e.g. "SIB1", "Paging"
    std::string summary;               // brief key fields, e.g. "plmn=250-01 tac=123"
    std::string raw;                   // payload as uppercase hex (may be empty)
    std::string detail;                // full decoded tree (asn_fprint), may be empty
    size_t      len      = 0;          // payload length (bytes)
};

using JournalSink = std::function<void(const JournalRecord&)>;

void journal_set_sink(JournalSink sink);
bool journal_enabled();
void journal_emit(const JournalRecord& rec);

// Uppercase hex of a byte buffer (for the `raw` field).
std::string journal_hex(const uint8_t* data, size_t len);
