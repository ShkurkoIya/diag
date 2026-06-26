#include "diag_nr_log_parser.h"
#include <cstring>

using namespace diag_util;

// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint16_t> DiagNrLogParser::handled_log_codes() {
    return {
            LOG_NR5G_ML1_MEAS_DB_UPDATE_C,   // 0xB97F
            LOG_NR5G_ML1_SERVING_CELL_MEAS_C,// 0xB992
    };
}

// ─────────────────────────────────────────────────────────────────────────────
void DiagNrLogParser::add_or_update_neighbor(const NrCell &c) {
    for (auto &n: neighbors_) {
        if (n.nrarfcn == c.nrarfcn && n.pci == c.pci) {
            n = c;
            return;
        }
    }
    neighbors_.push_back(c);
}

// ─────────────────────────────────────────────────────────────────────────────
bool DiagNrLogParser::parse(const uint8_t *buf, size_t len) {
    if (len < sizeof(LogRecord)) {
        DIAG_LOGW("NR: packet too short (%zu)", len);
        return false;
    }
    const auto *hdr = reinterpret_cast<const LogRecord *>(buf);
    const uint8_t *p = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);

    switch (hdr->code) {
        case LOG_NR5G_ML1_MEAS_DB_UPDATE_C:
            return parse_meas_db_update(p, plen);
        case LOG_NR5G_ML1_SERVING_CELL_MEAS_C:
            return parse_serving_cell_meas(p, plen);
        default:
            return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 0xB97F – NR5G ML1 Searcher Measurement Database Update (Ext)
//
// Subpacket container format (like LTE 0xB193):
//   NrLogPktHeader (4 bytes)
//   Then num_subpkts × NrLogSubPkt
//
// Each subpacket contains component carrier info with cells and beams.
//
// Typical layout within a CC subpacket:
//   uint32 word0: NR-ARFCN[23:0] (24 bits) | reserved
//   uint32 word1: num_cells[3:0] | reserved
//   Per cell:
//     uint32 w0: PCI[9:0] | num_beams[13:10] | reserved
//     Per beam:
//       uint32 bw0: beam_idx | rsrp_raw[21:10] | reserved
//       uint32 bw1: rsrq_raw | sinr_raw
// ─────────────────────────────────────────────────────────────────────────────
bool DiagNrLogParser::parse_meas_db_update(const uint8_t *p, size_t plen) {
    if (plen < sizeof(NrLogPktHeader)) {
        DIAG_LOGW("NR 0xB97F: payload too short (%zu)", plen);
        return false;
    }

    const auto *hdr = reinterpret_cast<const NrLogPktHeader *>(p);
    DIAG_LOGD("NR 0xB97F: ver=%u.%u num_subpkts=%u",
              hdr->major_ver, hdr->minor_ver, hdr->num_subpkts);

    size_t offset = sizeof(NrLogPktHeader);
    bool ok = false;

    for (uint8_t s = 0; s < hdr->num_subpkts; ++s) {
        if (offset + sizeof(NrLogSubPkt) > plen) break;

        const auto *sp = reinterpret_cast<const NrLogSubPkt *>(p + offset);
        size_t sp_hdr_sz = sizeof(NrLogSubPkt);
        size_t sp_data_sz = (sp->size > sp_hdr_sz) ? (sp->size - sp_hdr_sz) : 0;
        const uint8_t *sp_data = p + offset + sp_hdr_sz;

        DIAG_LOGD("  NR subpkt[%u] id=0x%02X ver=%u size=%u", s, sp->id, sp->ver, sp->size);

        if (offset + sp->size > plen) break;

        // Parse the component carrier subpacket
        if (sp_data_sz >= 8) {
            uint32_t w0 = read_u32(sp_data);
            uint32_t nrarfcn = bits(w0, 0, 24);// NR-ARFCN in lower 24 bits

            uint32_t w1 = read_u32(sp_data + 4);
            uint8_t num_cells = bits(w1, 0, 4);

            size_t cell_off = 8;

            for (uint8_t c = 0; c < num_cells; ++c) {
                if (cell_off + 4 > sp_data_sz) break;

                uint32_t cw0 = read_u32(sp_data + cell_off);
                uint16_t pci = bits(cw0, 0, 10);
                uint8_t num_beams = bits(cw0, 10, 4);
                cell_off += 4;

                // Read best beam's measurements (first beam)
                int16_t best_rsrp = -180;
                int16_t best_rsrq = -30;
                int16_t best_sinr = -20;

                for (uint8_t b = 0; b < num_beams; ++b) {
                    if (cell_off + 8 > sp_data_sz) break;

                    uint32_t bw0 = read_u32(sp_data + cell_off);
                    uint32_t bw1 = read_u32(sp_data + cell_off + 4);

                    uint32_t raw_rsrp = bits(bw0, 10, 12);
                    uint32_t raw_rsrq = bits(bw1, 0, 12);
                    uint32_t raw_sinr = bits(bw1, 12, 10);

                    int16_t rsrp = convert_nr_rsrp(raw_rsrp);
                    int16_t rsrq = convert_nr_rsrq(raw_rsrq);
                    int16_t sinr = convert_nr_sinr(raw_sinr);

                    if (b == 0 || rsrp > best_rsrp) {
                        best_rsrp = rsrp;
                        best_rsrq = rsrq;
                        best_sinr = sinr;
                    }

                    cell_off += 8;
                }

                NrCell cell{};
                cell.nrarfcn = nrarfcn;
                cell.pci = pci;
                cell.ss_rsrp = best_rsrp;
                cell.ss_rsrq = best_rsrq;
                cell.ss_sinr = best_sinr;
                cell.cell_id = -1;
                cell.serving = false;

                // Sanity: PCI 0-1007, NRARFCN < ~3300000
                if (pci > 1007 || nrarfcn > 3300000) {
                    DIAG_LOGW("NR meas[%u]: insane PCI=%u NRARFCN=%u, skip", c, pci, nrarfcn);
                    continue;
                }

                DIAG_LOGD("NR meas[%u]: NRARFCN=%u PCI=%u RSRP=%d RSRQ=%d SINR=%d",
                          c, cell.nrarfcn, cell.pci, cell.ss_rsrp, cell.ss_rsrq, cell.ss_sinr);
                add_or_update_neighbor(cell);
                if (cell_cb_) cell_cb_(cell);
                ok = true;
            }
        }

        offset += sp->size;
        if (sp->size == 0) break;
    }

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0xB992 – NR5G ML1 Serving Cell Measurement
//
// Similar subpacket container. The serving cell subpacket provides
// NR-ARFCN, PCI, and beam-level measurements.
// ─────────────────────────────────────────────────────────────────────────────
bool DiagNrLogParser::parse_serving_cell_meas(const uint8_t *p, size_t plen) {
    if (plen < sizeof(NrLogPktHeader)) {
        DIAG_LOGW("NR 0xB992: payload too short (%zu)", plen);
        return false;
    }

    const auto *hdr = reinterpret_cast<const NrLogPktHeader *>(p);
    DIAG_LOGD("NR 0xB992: ver=%u.%u num_subpkts=%u",
              hdr->major_ver, hdr->minor_ver, hdr->num_subpkts);

    size_t offset = sizeof(NrLogPktHeader);

    for (uint8_t s = 0; s < hdr->num_subpkts; ++s) {
        if (offset + sizeof(NrLogSubPkt) > plen) break;

        const auto *sp = reinterpret_cast<const NrLogSubPkt *>(p + offset);
        size_t sp_hdr_sz = sizeof(NrLogSubPkt);
        size_t sp_data_sz = (sp->size > sp_hdr_sz) ? (sp->size - sp_hdr_sz) : 0;
        const uint8_t *sp_data = p + offset + sp_hdr_sz;

        if (offset + sp->size > plen) break;

        // Try to extract serving cell from subpacket data
        if (sp_data_sz >= 12) {
            uint32_t w0 = read_u32(sp_data);
            uint32_t nrarfcn = bits(w0, 0, 24);

            uint32_t w1 = read_u32(sp_data + 4);
            uint16_t pci = bits(w1, 0, 10);

            // Measurement words
            int16_t rsrp = -180, rsrq = -30, sinr = -20;
            if (sp_data_sz >= 20) {
                uint32_t w_meas = read_u32(sp_data + 12);
                rsrp = convert_nr_rsrp(bits(w_meas, 0, 12));

                if (sp_data_sz >= 24) {
                    uint32_t w_meas2 = read_u32(sp_data + 16);
                    rsrq = convert_nr_rsrq(bits(w_meas2, 0, 12));
                    sinr = convert_nr_sinr(bits(w_meas2, 12, 10));
                }
            }

            if (pci <= 1007 && nrarfcn <= 3300000) {
                serving_.nrarfcn = nrarfcn;
                serving_.pci = pci;
                serving_.ss_rsrp = rsrp;
                serving_.ss_rsrq = rsrq;
                serving_.ss_sinr = sinr;
                serving_.cell_id = -1;
                serving_.serving = true;

                DIAG_LOGD("NR serving: NRARFCN=%u PCI=%u RSRP=%d RSRQ=%d SINR=%d",
                          nrarfcn, pci, rsrp, rsrq, sinr);
                if (cell_cb_) cell_cb_(serving_);
            }
        }

        offset += sp->size;
        if (sp->size == 0) break;
    }

    return serving_.nrarfcn != 0;
}
