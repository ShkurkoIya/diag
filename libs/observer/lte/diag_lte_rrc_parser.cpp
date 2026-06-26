#include "diag_lte_rrc_parser.h"
#include "asn1c_lte_bridge.h"// ← SIB1 decoding for foreign-cell capture
#include "item_utils.h"
#include "journal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace diag_util;

// ═════════════════════════════════════════════════════════════════════════════
// Log code registration
// ═════════════════════════════════════════════════════════════════════════════
std::vector<uint16_t> DiagLteRrcParser::handled_log_codes() {
    return {
            LOG_LTE_RRC_OTA_C,           // 0xB0C0
            LOG_LTE_RRC_MIB_C,           // 0xB0C1
            LOG_LTE_RRC_SERV_CELL_INFO_C,// 0xB0C2  ★ primary identity source
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// Dispatch
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteRrcParser::parse(const uint8_t *buf, size_t len) {
    if (len < sizeof(LogRecord)) return false;
    const auto *hdr = reinterpret_cast<const LogRecord *>(buf);
    const uint8_t *p = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);

    switch (hdr->code) {
        case LOG_LTE_RRC_SERV_CELL_INFO_C:
            return parse_rrc_serving_cell_info(p, plen);
        case LOG_LTE_RRC_MIB_C:
            return parse_rrc_mib(p, plen);
        case LOG_LTE_RRC_OTA_C:
            return parse_rrc_ota(p, plen);
        default:
            DIAG_LOGD("LTE RRC: unknown code 0x%04X", hdr->code);
            return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB0C2 — RRC Serving Cell Info
// (ported from your working diag_lte_nas_parser.cpp)
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteRrcParser::parse_rrc_serving_cell_info(const uint8_t *p, size_t plen) {
    if (plen < 1) return false;

    uint8_t ver = p[0];

    // Session counter — track how many B0C2 packets arrive
    static uint64_t s_b0c2_total = 0;
    s_b0c2_total++;

    static bool s_first_b0c2_dumped = false;
    if (!s_first_b0c2_dumped) {
        s_first_b0c2_dumped = true;
        std::fprintf(stderr, "[B0C2] FIRST packet ver=%u plen=%zu hex:", ver, plen);
        for (size_t i = 0; i < std::min<size_t>(plen, 40); ++i)
            std::fprintf(stderr, " %02X", p[i]);
        std::fprintf(stderr, "\n");
    }

    // Periodic summary — B0C2 is rare, log every packet for the first few
    if (s_b0c2_total <= 10 || s_b0c2_total % 5 == 0) {
        DIAG_LOGI("[B0C2] count=%llu (ver=%u plen=%zu)",
                  (unsigned long long) s_b0c2_total, ver, plen);
    }

    if (debug_) {
        DIAG_LOGD("LTE 0xB0C2 ver=%u plen=%zu", ver, plen);
        diag_util::hex_dump("0xB0C2", p, plen, 40);
    }

    // ── v3 path (SM8550) ─────────────────────────────────────────────
    if (ver == 3 && plen >= 29) {
        QcDiagLteRrcServCellInfo_v3 info{};
        if (!parse_qcdiag_lte_rrc_serv_cellinfo_v3(p, plen, info)) {
            DIAG_LOGW("B0C2 v3: parse failed plen=%zu", plen);
            return false;
        }
        uint32_t cell_id = info.cell_id & 0x0FFFFFFF;
        std::fprintf(stderr,
                     "[B0C2-v3] decoded pci=%u dl_earfcn=%u ul_earfcn=%u cell_id=0x%07X "
                     "tac=0x%04X band=%u mcc=%u mnc=%u%s\n",
                     info.pci, info.dl_earfcn, info.ul_earfcn, cell_id, info.tac,
                     info.band, info.mcc, info.mnc,
                     (info.dl_earfcn == 0 || cell_id == 0) ? "  <-- REJECTED (earfcn/cid=0)" : "  [OK]");
        if (info.dl_earfcn == 0 || cell_id == 0) return false;

        id_.cell_id = cell_id;
        id_.tac = info.tac;
        id_.mcc = info.mcc;
        id_.mnc = info.mnc;
        id_.mnc_digit_count = info.mnc_digit_count;
        id_.earfcn = info.dl_earfcn;
        id_.ul_earfcn = info.ul_earfcn;
        id_.pci = info.pci;
        id_.band = info.band;
        id_.dl_bw = info.dl_bw;
        id_.ul_bw = info.ul_bw;
        id_.allowed_access = info.allowed_access;
        id_.valid = true;

        char mnc_str[8];
        if (info.mnc_digit_count == 3) std::snprintf(mnc_str, sizeof(mnc_str), "%03u", info.mnc);
        else
            std::snprintf(mnc_str, sizeof(mnc_str), "%02u", info.mnc);

        DIAG_LOGI("LTE RRC SCell v3: EARFCN=%u/%u Band=%u BW=%u/%u "
                  "PCI=%u CID=0x%07X TAC=0x%04X MCC=%u MNC=%s",
                  info.dl_earfcn, info.ul_earfcn, info.band,
                  info.dl_bw, info.ul_bw, info.pci, cell_id, info.tac,
                  info.mcc, mnc_str);

        stash_serving_cell();
        if (id_cb_) id_cb_(id_);
        return true;
    }

    // ── v2 path ───────────────────────────────────────────────────────
    if (ver == 2 && plen >= 25) {
        QcDiagLteRrcServCellInfo_v2 info{};
        if (!parse_qcdiag_lte_rrc_serv_cellinfo_v2(p, plen, info)) return false;
        uint32_t cell_id = info.cell_id & 0x0FFFFFFF;
        if (info.dl_earfcn == 0 || cell_id == 0) return false;

        id_.cell_id = cell_id;
        id_.tac = info.tac;
        id_.mcc = info.mcc;
        id_.mnc = info.mnc;
        id_.mnc_digit_count = info.mnc_digit_count;
        id_.earfcn = info.dl_earfcn;
        id_.ul_earfcn = info.ul_earfcn;
        id_.pci = info.pci;
        id_.band = info.band;
        id_.dl_bw = info.dl_bw;
        id_.ul_bw = info.ul_bw;
        id_.allowed_access = info.allowed_access;
        id_.valid = true;

        char mnc_str[8];
        if (info.mnc_digit_count == 3) std::snprintf(mnc_str, sizeof(mnc_str), "%03u", info.mnc);
        else
            std::snprintf(mnc_str, sizeof(mnc_str), "%02u", info.mnc);

        DIAG_LOGI("LTE RRC SCell v2: EARFCN=%u/%u Band=%u BW=%u/%u "
                  "PCI=%u CID=0x%07X TAC=0x%04X MCC=%u MNC=%s",
                  info.dl_earfcn, info.ul_earfcn, info.band,
                  info.dl_bw, info.ul_bw, info.pci, cell_id, info.tac,
                  info.mcc, mnc_str);

        stash_serving_cell();
        if (id_cb_) id_cb_(id_);
        return true;
    }

    // ── Legacy v0/v1 ──────────────────────────────────────────────────
    if (plen >= 14) {
        uint32_t earfcn = diag_util::rd16(p + 4);
        uint32_t cell_id = diag_util::rd32(p + 6) & 0x0FFFFFFF;
        uint16_t pci = 0;
        if (plen >= 14) {
            uint16_t pss = diag_util::rd16(p + 10);
            uint16_t sss = diag_util::rd16(p + 12);
            if (pss <= 2 && sss <= 167) pci = sss * 3 + pss;
        }
        if (earfcn > 0 && cell_id > 0) {
            id_.earfcn = earfcn;
            id_.cell_id = cell_id;
            if (pci > 0 && pci <= 503) id_.pci = pci;
            id_.valid = (id_.mcc != 0);
            DIAG_LOGI("LTE RRC SCell legacy: EARFCN=%u PCI=%u CID=0x%07X (no MCC/MNC/TAC)",
                      earfcn, pci, cell_id);
            if (id_cb_) id_cb_(id_);
            return true;
        }
    }

    DIAG_LOGW("B0C2: unsupported ver=%u plen=%zu", ver, plen);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// stash_serving_cell — keep EVERY camped cell's identity, not just the latest.
//
// 0xB0C2 fires once per cell the modem camps on while sweeping. id_ holds only
// the most recent; without this, merge_lte_identity() could stamp identity onto
// just one ML1 measurement and every earlier serving cell ended up signal-only.
// Upsert by (earfcn,pci); merge_lte_identity() drains serv_cells_ each cycle.
// ─────────────────────────────────────────────────────────────────────────────
void DiagLteRrcParser::stash_serving_cell() {
    if (!id_.valid || id_.earfcn == 0) return;
    for (auto &c: serv_cells_) {
        if (c.earfcn == id_.earfcn && c.pci == id_.pci) {
            c.cell_id = static_cast<int32_t>(id_.cell_id);
            c.tac = id_.tac;
            c.mcc = id_.mcc;
            c.mnc = id_.mnc;
            if (id_.dl_bw) c.dl_bw = id_.dl_bw;
            if (id_.ul_bw) c.ul_bw = id_.ul_bw;
            return;
        }
    }
    LteCell c{};
    c.earfcn = id_.earfcn;
    c.pci = id_.pci;
    c.cell_id = static_cast<int32_t>(id_.cell_id);
    c.tac = id_.tac;
    c.mcc = id_.mcc;
    c.mnc = id_.mnc;
    c.dl_bw = id_.dl_bw;
    c.ul_bw = id_.ul_bw;
    c.serving = false;// identity record; serving status decided by the merge
    serv_cells_.push_back(c);
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB0C1 — RRC MIB Message
// (ported from your working diag_lte_nas_parser.cpp)
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteRrcParser::parse_rrc_mib(const uint8_t *p, size_t plen) {
    if (plen < 1) return false;

    const uint8_t ver = p[0];

    if (debug_) {
        DIAG_LOGD("LTE 0xB0C1 MIB: ver=%u plen=%zu", ver, plen);
        hex_dump("0xB0C1", p, plen, 48);
    }

    if (ver == 1) {
        QcDiagLteMib_v1 item;
        if (!parse_qcdiag_lte_mib_v1(p, plen, item)) return false;
        id_.sfn = item.sfn;
        DIAG_LOGD("LTE MIB v1: bandwidth=%u tx_antenna=%u sfn=%u earfcn=%u pci=%u",
                  item.bandwidth, item.tx_antenna, item.sfn, item.earfcn, item.pci);
    } else if (ver == 2) {
        QcDiagLteMib_v2 item;
        if (!parse_qcdiag_lte_mib_v2(p, plen, item)) return false;
        id_.sfn = item.sfn;
        DIAG_LOGD("LTE MIB v2: bandwidth=%u tx_antenna=%u sfn=%u earfcn=%u pci=%u",
                  item.bandwidth, item.tx_antenna, item.sfn, item.earfcn, item.pci);
    } else if (ver == 17) {
        QcDiagLteMib_v17 item;
        if (!parse_qcdiag_lte_mib_v17(p, plen, item)) return false;
        id_.sfn = item.sfn;
        DIAG_LOGD("LTE MIB v17: tx_antenna=%u sfn=%u earfcn=%u pci=%u opmode=%u "
                  "access_barring=%u si_value_tag=%u sfn_msb4=%u hsfn_lsb2=%u "
                  "sib1_sch_info=%u",
                  item.tx_antenna, item.sfn, item.earfcn, item.pci, item.opmode_type,
                  item.access_barring, item.si_value_tag, item.sfn_msb4, item.hsfn_lsb2,
                  item.sib1_sch_info);
    } else {
        DIAG_LOGD("B0C1 MIB: unsupported ver=%u", ver);
        return false;
    }
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB0C0 — RRC OTA Packet
// (ported from your working diag_lte_nas_parser.cpp — keeps scat-style logic)
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteRrcParser::parse_rrc_ota(const uint8_t *p, size_t plen) {
    if (plen < 2) return false;

    const uint8_t ver = p[0];

    if (debug_) {
        DIAG_LOGD("LTE 0xB0C0 RRC OTA: ver=%u plen=%zu", ver, plen);
        hex_dump("0xB0C0", p, plen, 64);
    }

    if (ver >= 30) {
        DIAG_LOGD("B0C0: ver=%u not yet handled", ver);
        return false;
    }

    if (ver >= 25) {
        ItemV25 item{};
        if (!parse_item_v25(p, plen, item)) return false;

        if (debug_) {
            DIAG_LOGD("v25 item: rrc=%u.%u nr_rrc=%u.%u rbid=%u pci=%u earfcn=%u "
                      "sfn_subfn=%u pdu_num=%u sib_mask=%u len=%u",
                      item.rrc_rel_maj, item.rrc_rel_min,
                      item.nr_rrc_rel_maj, item.nr_rrc_rel_min, item.rbid,
                      item.pci, item.earfcn, item.sfn_subfn, item.pdu_num,
                      item.sib_mask, item.len);
        }

        const uint8_t *payload = p + 21;
        size_t payload_avail = (plen > 21) ? (plen - 21) : 0;
        size_t payload_len = std::min<size_t>(item.len, payload_avail);
        if (payload_len == 0) return false;

        // ── Packet journal: one entry per RRC PDU (channel + message type) ──
        if (journal_enabled()) {
            const char *chan = "RRC";
            const char *msg = "RRC";
            const char *pdu_name = nullptr;
            switch (item.pdu_num) {
                case 1:
                    chan = "BCCH-BCH";
                    msg = "MasterInformationBlock";
                    pdu_name = "BCCH-BCH-Message";
                    break;
                case 3:
                    chan = "BCCH-DL-SCH";
                    msg = "SIB1/SystemInformation";
                    pdu_name = "BCCH-DL-SCH-Message";
                    break;
                case 6:
                    chan = "MCCH";
                    msg = "MCCH";
                    pdu_name = "MCCH-Message";
                    break;
                case 7:
                    chan = "PCCH";
                    msg = "Paging";
                    pdu_name = "PCCH-Message";
                    break;
                case 8:
                    chan = "DL-CCCH";
                    msg = "DL-CCCH";
                    pdu_name = "DL-CCCH-Message";
                    break;
                case 9:
                    chan = "DL-DCCH";
                    msg = "DL-DCCH";
                    pdu_name = "DL-DCCH-Message";
                    break;
                case 10:
                    chan = "UL-CCCH";
                    msg = "UL-CCCH";
                    pdu_name = "UL-CCCH-Message";
                    break;
                case 11:
                    chan = "UL-DCCH";
                    msg = "UL-DCCH (MeasurementReport?)";
                    pdu_name = "UL-DCCH-Message";
                    break;
                default:
                    break;
            }
            char sbuf[96];
            snprintf(sbuf, sizeof(sbuf), "pdu=%u pci=%u earfcn=%u sfn=%u",
                     item.pdu_num, item.pci, item.earfcn, item.sfn_subfn);
            JournalRecord jr;
            jr.t = static_cast<double>(time(nullptr));
            jr.code = 0xB0C0;
            jr.rat = "LTE_RRC";
            jr.channel = chan;
            jr.msg_type = msg;
            jr.summary = sbuf;
            jr.raw = journal_hex(payload, payload_len);
            if (pdu_name) jr.detail = lte_asn1::decode_pdu_text(payload, payload_len, pdu_name);
            // Structural message label for BCCH-DL-SCH (SIB1 vs SI[sibN]) — taken
            // from the decoded structure rather than scanning printed text.
            if (item.pdu_num == 3) {
                auto dr = lte_asn1::decode_pdu(payload, payload_len, "BCCH-DL-SCH-Message");
                if (!dr.message_type.empty()) jr.msg_type = dr.message_type;
                else if (!dr.ok)
                    jr.msg_type = "SystemInformation (decode failed)";
                if (dr.sib1) {
                    const auto &s = *dr.sib1;
                    char hb[48];
                    snprintf(hb, sizeof(hb), " tac=0x%04X cid=0x%07X band=%u",
                             s.tac, s.cell_id, s.freq_band);
                    jr.summary += hb;
                    if (!s.plmn_list.empty()) {
                        std::string plmns;
                        for (size_t k = 0; k < s.plmn_list.size() && k < 4; ++k) {
                            if (!plmns.empty()) plmns += ",";
                            plmns += s.plmn_list[k].mcc + "-" + s.plmn_list[k].mnc;
                        }
                        jr.summary += " plmn=" + plmns;
                    }
                }
            }
            jr.len = payload_len;
            journal_emit(jr);
        }

        // PDU type mapping per scat (v19, v26, v27, v29, v30)
        switch (item.pdu_num) {
            case 1:// BCCH_BCH (MIB)
                return parse_rrc_mib(payload, payload_len);
            case 3:// BCCH_DL_SCH (SIB1/SystemInformation) ★ foreign cells arrive here
                return parse_rrc_sib1(payload, payload_len, item.earfcn, item.pci);
            case 11:// UL_DCCH — may carry MeasurementReport (rsrp/rsrq) ★
                return parse_rrc_ul_dcch(payload, payload_len, item.earfcn, item.pci);
            case 6: // MCCH
            case 7: // PCCH
            case 8: // DL_CCCH
            case 9: // DL_DCCH
            case 10:// UL_CCCH
                return true;
            default:
                DIAG_LOGI("[B0C0] unsupported pdu_num=%u", item.pdu_num);
                return false;
        }
    }

    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB0C0 BCCH_DL_SCH (pdu_num=3) — SystemInformation including SIB1.
//
// THIS is how we discover foreign-operator cells. During AT+COPS=? PLMN
// search, the modem briefly tunes to each visible cell and decodes its
// SIB1 broadcast. The decoded RRC packet arrives here via 0xB0C0 with
// pdu_num=3 (BCCH-DL-SCH-Message containing SystemInformationBlockType1).
//
// We ASN.1-decode it via the bundled asn1c bridge and register every
// PLMN found as a separate cell record — that's how we get MCC/MNC/CID/TAC
// of MegaFon/Beeline/Tele2/YOTA cells even though we're attached to MTS.
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteRrcParser::parse_rrc_sib1(const uint8_t *p, size_t plen,
                                      uint32_t earfcn, uint16_t pci) {
    if (plen == 0) return false;

    auto result = lte_asn1::decode_pdu(p, plen, "BCCH-DL-SCH-Message");

    // SIB4/SIB5 (periodic SystemInformation) carry neighbor PCI→EARFCN maps.
    // Store them regardless of whether this PDU also had SIB1. SIB4 entries
    // have earfcn=0 (intra-freq) → resolve to the serving earfcn here.
    if (result.ok && !result.neighbor_freqs.empty()) {
        for (const auto &nf: result.neighbor_freqs) {
            uint32_t e = nf.inter_freq ? nf.earfcn : earfcn;// SIB4 → serving freq
            if (nf.pci >= 0 && e > 0) {
                neigh_freq_map_[nf.pci] = e;
                DIAG_LOGI("[SIB%d] neigh PCI=%d → EARFCN=%u",
                          nf.inter_freq ? 5 : 4, nf.pci, e);
            }
        }
    }

    if (!result.ok || !result.sib1) {
        // Not a SIB1 (could be an SI message we just harvested neighbors from).
        if (debug_ && !result.ok) {
            DIAG_LOGD("[B0C0/SIB1] decode failed (plen=%zu, earfcn=%u, pci=%u): %s",
                      plen, earfcn, pci, result.error.c_str());
        }
        return true;// not an error — neighbor freqs (if any) were stored
    }

    const auto &s = *result.sib1;
    if (s.plmn_list.empty()) {
        if (debug_) DIAG_LOGD("[B0C0/SIB1] no PLMN list");
        return false;
    }

    // Register each PLMN as a separate cell record. Some cells advertise
    // multiple PLMNs (shared infrastructure — e.g. a tower owned by MegaFon
    // but also broadcasting MTS PLMN id). Each becomes its own entry so the
    // scan output reflects the full operator landscape.
    for (const auto &plmn: s.plmn_list) {
        LteCell c{};
        c.earfcn = earfcn;
        c.pci = pci;
        c.cell_id = static_cast<int64_t>(s.cell_id);
        c.tac = static_cast<int>(s.tac);
        try {
            c.mcc = std::stoi(plmn.mcc);
            c.mnc = std::stoi(plmn.mnc);
        } catch (...) { continue; }
        c.serving = false;// SIB1 from B0C0 is broadcast — modem decoded
                          // it during PLMN search or background measurement.
                          // The merge layer in qualcomm_log_parser will mark
                          // it serving if it matches the active camp.

        // Push to local accumulator — merge_lte_identity() drains this into
        // the unified neighbor list shared with ML1.
        sib1_cells_.push_back(c);

        DIAG_LOGI("[SIB1] EARFCN=%u PCI=%u MCC=%d MNC=%d CID=%d TAC=%d "
                  "barred=%d csg=%d",
                  earfcn, pci, c.mcc, c.mnc, c.cell_id, c.tac,
                  s.cell_barred ? 1 : 0, s.csg_indication ? 1 : 0);
    }

    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// 0xB0C0 UL_DCCH (pdu_num=11) — MeasurementReport.
//
// When the UE is RRC_CONNECTED with measurements configured (which happens
// once we lock/camp onto a cell during active scan), it sends MeasurementReport
// on UL-DCCH. This carries rsrp/rsrq of the serving (PCell) and EUTRA neighbor
// cells — the ONLY RRC channel that contains signal measurements.
//
// We push these into meas_results_ tagged with the OTA packet's earfcn/pci
// (which is the serving cell). The merge layer matches them by PCI against
// already-scanned cells and fills in any missing rsrp/rsrq.
// ═════════════════════════════════════════════════════════════════════════════
bool DiagLteRrcParser::parse_rrc_ul_dcch(const uint8_t *p, size_t plen,
                                         uint32_t earfcn, uint16_t pci) {
    if (plen == 0) return false;

    auto result = lte_asn1::decode_pdu(p, plen, "UL-DCCH-Message");
    if (!result.ok || result.meas_results.empty()) {
        // Not a MeasurementReport (could be any other UL-DCCH message) —
        // not an error, just nothing to extract.
        return true;
    }

    for (const auto &m: result.meas_results) {
        LteMeasReport r{};
        // Serving: use the OTA packet's earfcn/pci.
        // Neighbors: resolve the carrier frequency from the SIB4/SIB5 PCI→EARFCN
        // map (SIB5 gives inter-freq carriers exactly; SIB4 gives intra-freq =
        // serving earfcn). If the PCI isn't in the map, fall back to the serving
        // earfcn (intra-freq assumption — correct for the common case).
        if (m.is_serving) {
            r.earfcn = earfcn;
            r.pci = pci;
        } else {
            auto it = neigh_freq_map_.find(m.pci);
            r.earfcn = (it != neigh_freq_map_.end()) ? it->second : earfcn;
            r.pci = m.pci;
        }
        r.rsrp = m.rsrp_dbm;
        r.rsrq = m.rsrq_db;
        r.is_serving = m.is_serving;
        meas_reports_.push_back(r);

        DIAG_LOGI("[MEAS] %s EARFCN=%u PCI=%d RSRP=%d RSRQ=%d%s",
                  m.is_serving ? "PCell" : "neigh",
                  r.earfcn, r.pci, r.rsrp, r.rsrq,
                  (!m.is_serving && neigh_freq_map_.count(m.pci)) ? " (SIB-resolved)" : "");
    }
    return true;
}
