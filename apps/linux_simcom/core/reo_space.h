#ifndef REO_SPACE_H
#define REO_SPACE_H

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <qualcomm_log_parser.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReoSpace {
    // ─────────────────────────────────────────────────────────────────────────────
    // Cell database
    // ─────────────────────────────────────────────────────────────────────────────
    struct CellRecord {
        enum class Type {
            GSM,
            WCDMA,
            LTE,
            UMTS,
            NR
        } type;

        uint32_t freq = 0;    // ARFCN/UARFCN/EARFCN/NRARFCN
        uint32_t id = 0;      // PCI / PSC / BSIC
        int16_t rsrp_rscp = 0;// dBm
        int16_t rsrq_ecno = 0;// dB
        int16_t rssi = 0;
        uint8_t dl_bw = 0;// DL bandwidth (raw B0C2 field; 0 = unknown)
        uint8_t ul_bw = 0;// UL bandwidth (raw B0C2 field; 0 = unknown)
        bool serving = false;

        // Identity fields
        int32_t cell_id = -1;// E-UTRAN CID / UTRAN CID / GSM CID
        uint16_t tac_lac = 0;// TAC (LTE/NR) or LAC (GSM/WCDMA)
        uint32_t mcc = 0;
        uint32_t mnc = 0;

        // GSM-only extras
        uint8_t ncc = 0xFF;    // 0xFF = unknown
        uint8_t bcc = 0xFF;    // 0xFF = unknown
        int16_t c1 = INT16_MIN;// INT16_MIN = unknown
        int16_t c2 = INT16_MIN;// INT16_MIN = unknown

        // Metadata
        time_t first_seen = 0;
        time_t last_seen = 0;
        uint32_t seen_count = 0;

        std::string signal_bar() const {
            int v = rsrp_rscp;
            int bars;
            if (type == Type::LTE || type == Type::NR) {
                if (v >= -80)
                    bars = 5;
                else if (v >= -95)
                    bars = 4;
                else if (v >= -105)
                    bars = 3;
                else if (v >= -115)
                    bars = 2;
                else
                    bars = 1;
            } else if (type == Type::WCDMA || type == Type::UMTS) {
                if (v >= -75)
                    bars = 5;
                else if (v >= -85)
                    bars = 4;
                else if (v >= -95)
                    bars = 3;
                else if (v >= -105)
                    bars = 2;
                else
                    bars = 1;
            } else {
                if (v >= -70)
                    bars = 5;
                else if (v >= -80)
                    bars = 4;
                else if (v >= -90)
                    bars = 3;
                else if (v >= -100)
                    bars = 2;
                else
                    bars = 1;
            }
            std::string s;
            for (int i = 0; i < 5; ++i)
                s += (i < bars) ? "█" : "░";
            return s;
        }

        // Format MCCMNC string e.g. "25011" or "---"
        std::string mccmnc_str() const {
            if (mcc == 0)
                return "---";
            char buf[16];
            if (mnc < 10)
                snprintf(buf, sizeof(buf), "%u%02u", mcc, mnc);
            else
                snprintf(buf, sizeof(buf), "%u%u", mcc, mnc);
            return buf;
        }

        // Format CID string e.g. "19981501" or "-"
        std::string cid_str() const {
            if (cell_id < 0)
                return "-";
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", cell_id);
            return buf;
        }

        // Format TAC/LAC string
        std::string taclac_str() const {
            if (tac_lac == 0)
                return "-";
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", tac_lac);
            return buf;
        }
    };

    struct CellDb {
        std::mutex mx;
        std::unordered_map<uint64_t, CellRecord> cells;
        uint64_t update_seq = 0;

        uint64_t key(CellRecord::Type t, uint32_t freq, uint32_t id) const {
            return ((uint64_t) t << 48) | ((uint64_t) (freq & 0xFFFFF) << 16) | (id & 0xFFFF);
        }

        void upsert(CellRecord rec) {
            uint64_t k;
            if (rec.type == CellRecord::Type::GSM && rec.cell_id > 0) {
                // CID is the AUTHORITATIVE GSM cell identity. Key by CID regardless
                // of ARFCN so: (a) a mis-decoded BSIC can't split one physical cell
                // into duplicates (same CID + different BSIC is still ONE cell), and
                // (b) two distinct cells reusing a BCCH ARFCN (different CID) stay
                // separate. Bit 47 separates CID-keyed from ARFCN-keyed entries.
                k = ((uint64_t) rec.type << 48) | (1ULL << 47) |
                    ((uint32_t) rec.cell_id & 0xFFFF);
            } else if (rec.type == CellRecord::Type::GSM && rec.freq != 0) {
                // No CID yet (measurement-only neighbour): key by ARFCN so the L1
                // RxLev read and a later same-ARFCN identity read line up. BSIC is
                // deliberately NOT in the key — it is noisy and would split a cell.
                k = ((uint64_t) rec.type << 48) | ((uint64_t) (rec.freq & 0xFFFFF) << 16);
            } else {
                k = key(rec.type, rec.freq, rec.id);
            }
            std::lock_guard<std::mutex> lk(mx);
            auto it = cells.find(k);
            if (it == cells.end()) {
                rec.first_seen = rec.last_seen = time(nullptr);
                rec.seen_count = 1;
                cells[k] = rec;
            } else {
                CellRecord &e = it->second;
                // Only overwrite signal when the new record actually carries a
                // measurement. RRC-identity updates (CID/TAC, signal=0) must NOT
                // wipe the ML1 RSRP/RSRQ/RSSI captured for the same cell.
                if (rec.rsrp_rscp != 0)
                    e.rsrp_rscp = rec.rsrp_rscp;
                if (rec.rsrq_ecno != 0)
                    e.rsrq_ecno = rec.rsrq_ecno;
                if (rec.rssi != 0)
                    e.rssi = rec.rssi;
                if (rec.dl_bw)
                    e.dl_bw = rec.dl_bw;
                if (rec.ul_bw)
                    e.ul_bw = rec.ul_bw;
                e.serving = rec.serving;
                // Identity-overwrite guard: if this row already has an established
                // PLMN+CID and the incoming record carries a DIFFERENT non-zero CID,
                // it's a different physical cell that merely shares the key — keep
                // the established identity (don't clobber PLMN/CID/LAC).
                const bool identity_locked =
                        (e.mcc > 0 && e.cell_id > 0 && rec.cell_id > 0 && rec.cell_id != e.cell_id);
                if (!identity_locked) {
                    if (rec.cell_id >= 0)
                        e.cell_id = rec.cell_id;
                    if (rec.tac_lac > 0)
                        e.tac_lac = rec.tac_lac;
                    if (rec.mcc > 0) {
                        e.mcc = rec.mcc;
                        e.mnc = rec.mnc;
                    }
                }
                if (rec.type == CellRecord::Type::GSM && rec.id != 0xFF)
                    e.id = rec.id;// BSIC from L1 meas; SI-3 identity reads carry 0xFF
                if (rec.ncc != 0xFF)
                    e.ncc = rec.ncc;
                if (rec.bcc != 0xFF)
                    e.bcc = rec.bcc;
                if (rec.c1 != INT16_MIN)
                    e.c1 = rec.c1;
                if (rec.c2 != INT16_MIN)
                    e.c2 = rec.c2;
                e.last_seen = time(nullptr);
                e.seen_count++;
            }
            update_seq++;
        }

        std::vector<CellRecord> snapshot() {
            std::lock_guard<std::mutex> lk(mx);
            std::vector<CellRecord> v;
            v.reserve(cells.size());
            for (auto &kv: cells)
                v.push_back(kv.second);
            std::sort(v.begin(), v.end(), [](const CellRecord &a, const CellRecord &b) {
            if (a.serving != b.serving) return a.serving > b.serving;
            if (a.type != b.type) return a.type < b.type;
            return a.rsrp_rscp > b.rsrp_rscp; });
            return v;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // NAS log ring buffer
    // ─────────────────────────────────────────────────────────────────────────────
    struct NasEntry {
        time_t ts;
        bool uplink;
        uint8_t pd, msg_type;
        std::string name;
        size_t pdu_len;
    };
    struct NasLog {
        std::mutex mx;
        std::vector<NasEntry> entries;
        static constexpr size_t MAX = 40;
        void push(NasEntry e) {
            std::lock_guard<std::mutex> lk(mx);
            entries.push_back(std::move(e));
            if (entries.size() > MAX)
                entries.erase(entries.begin());
        }
        std::vector<NasEntry> snapshot() {
            std::lock_guard<std::mutex> lk(mx);
            return entries;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Reo helpers
    // ─────────────────────────────────────────────────────────────────────────────

    static const char *rat_name(Rat r) {
        switch (r) {
            case Rat::GSM:
                return "GSM";
            case Rat::WCDMA:
                return "WCDMA";
            case Rat::UMTS:
                return "UMTS";
            case Rat::NR:
                return "NR";
            case Rat::LTE_RRC:
                return "LTE_RRC";
            case Rat::LTE_NAS:
                return "LTE_NAS";
            case Rat::LTE:
                return "LTE";
            default:
                return "UNKNOWN";
        }
    }

}// namespace ReoSpace

#endif
