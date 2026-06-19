#include "journal.h"

namespace {
    JournalSink g_sink;
}

void journal_set_sink(JournalSink sink) { g_sink = std::move(sink); }
bool journal_enabled() { return static_cast<bool>(g_sink); }
void journal_emit(const JournalRecord& rec) { if (g_sink) g_sink(rec); }

std::string journal_hex(const uint8_t* data, size_t len) {
    static const char H[] = "0123456789ABCDEF";
    std::string s;
    if (!data) return s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) { s += H[data[i] >> 4]; s += H[data[i] & 0xF]; }
    return s;
}
