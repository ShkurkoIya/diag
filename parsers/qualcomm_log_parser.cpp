#include "qualcomm_log_parser.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include "journal.h"

using namespace diag_util;

// ═════════════════════════════════════════════════════════════════════════════
// Constructor — wire up per-RAT callbacks + build routing table
// ═════════════════════════════════════════════════════════════════════════════
QualcommLogParser::QualcommLogParser()
{
    // ── Per-RAT cell callbacks ───────────────────────────────────────
    gsm_.set_cell_callback([this](const GsmCell &)
                           { fire_neighbor_update(); });
    wcdma_.set_cell_callback([this](const WcdmaCell &)
                             { fire_neighbor_update(); });
    nr_.set_cell_callback([this](const NrCell &)
                          { fire_neighbor_update(); });

    // ── LTE ML1 measurement callback — triggers merge with identity ──
    lte_ml1_.set_cell_callback([this](const LteCell &)
                               {
        merge_lte_identity();
        fire_neighbor_update(); });

    // ── LTE RRC identity callback (authoritative MCC/MNC/CID/TAC) ────
    lte_rrc_.set_identity_callback([this](const LteCellIdentity &)
                                   {
        merge_lte_identity();
        fire_neighbor_update(); });

    // ── LTE NAS identity callback (fallback) ─────────────────────────
    lte_nas_.set_identity_callback([this](const LteCellIdentity &)
                                   {
        merge_lte_identity();
        fire_neighbor_update(); });

    // ── Build routing table ──────────────────────────────────────────
    for (uint16_t code : DiagGsmLogParser::handled_log_codes())
        code_to_rat_[code] = Rat::GSM;
    for (uint16_t code : DiagWcdmaLogParser::handled_log_codes())
        code_to_rat_[code] = Rat::WCDMA;
    for (uint16_t code : DiagLteRrcParser::handled_log_codes())
        code_to_rat_[code] = Rat::LTE_RRC;
    for (uint16_t code : DiagLteNasParser::handled_log_codes())
        code_to_rat_[code] = Rat::LTE_NAS;
    for (uint16_t code : DiagLteMl1Parser::handled_log_codes())
        code_to_rat_[code] = Rat::LTE;
    for (uint16_t code : DiagUmtsLogParser::handled_log_codes())
        code_to_rat_[code] = Rat::UMTS;
    for (uint16_t code : DiagNrLogParser::handled_log_codes())
        code_to_rat_[code] = Rat::NR;
}

void QualcommLogParser::set_nas_callback(NasCallback cb)
{
    umts_.set_nas_callback(std::move(cb));
}
void QualcommLogParser::set_rrc_callback(RrcCallback cb)
{
    umts_.set_rrc_callback(std::move(cb));
}

// ═════════════════════════════════════════════════════════════════════════════
// on_log — entry point for DCI log stream callbacks
// ═════════════════════════════════════════════════════════════════════════════
bool QualcommLogParser::on_log(const uint8_t *buf, size_t len)
{
    if (!buf || len < sizeof(LogRecord))
    {
        DIAG_LOGW("QCP: on_log: buffer too small (%zu)", len);
        return false;
    }

    // The DCI log stream callback (diag_register_dci_stream_proc) delivers the
    // INNER log record directly: [len:2][code:2][timestamp:8][payload...], i.e.
    // log code at offset +2 (confirmed by diag_dci_sample.c: *(uint16*)(ptr+2)).
    // There is NO outer 0x10/DIAG_LOG_F wrapper on this path.
    //
    // The previous code auto-unwrapped when buf[0] == DIAG_LOG_F (0x10). But
    // buf[0] here is the LOW byte of LogRecord.len, so that test misfires for
    // every record whose total length ends in 0x10 (e.g. a 272-byte 0xB193 =
    // 0x0110), skipping 6 real bytes and corrupting the whole packet. Removed.
    const uint8_t *rec_buf = buf;
    size_t rec_len = len;

    const auto *rec = reinterpret_cast<const LogRecord *>(rec_buf);

    if (rec->len > rec_len)
    {
        DIAG_LOGW("QCP: on_log: rec->len=%u > buf(%zu), clamping", rec->len, rec_len);
    }

    uint16_t code = rec->code;
    uint64_t ts = rec->timestamp;

    DIAG_LOGD("QCP: log code=0x%04X len=%zu", code, rec_len);

    if (raw_cb_)
    {
        DiagFrame frame{};
        frame.log_code = code;
        frame.timestamp_raw = ts;
        frame.timestamp_unix = qct_timestamp_to_unix(ts);
        frame.rat = log_code_rat(code);
        frame.payload = rec_buf + sizeof(LogRecord);
        frame.payload_len = (rec_len > sizeof(LogRecord))
                                ? rec_len - sizeof(LogRecord)
                                : 0;
        raw_cb_(frame);
    }

    return dispatch_log(code, rec_buf, rec_len, ts);
}

// Qualcomm DIAG event names (scat-authoritative ids). Event 500 =
// GPRS_SURROUND_SEARCH_START carries the ARFCN of the neighbour the modem is
// about to read — we feed it to the GSM parser as the surround-search hint so
// the following SI-3 (CID/LAI) becomes a full ARFCN+CID+LAC neighbour row.
static const char *qcp_event_name(uint16_t id)
{
    switch (id)
    {
    case 500:
        return "GPRS_SURROUND_SEARCH_START";
    case 501:
        return "GPRS_SURROUND_SEARCH_END";
    case 477:
        return "GSM_RESELECT_START";
    case 478:
        return "GSM_RESELECT_END";
    case 479:
        return "GSM_CAMP_ATTEMPT_START";
    case 484:
        return "GSM_RR_IN_SERVICE";
    case 486:
        return "GSM_CAMP_ATTEMPT_END";
    case 487:
        return "GSM_CELL_SELECTION_START";
    case 488:
        return "GSM_CELL_SELECTION_END";
    case 1795:
        return "LTE_RRC_NEW_CELL_IND";
    case 481:
        return "WCDMA_NEW_REFERENCE_CELL";
    default:
        return nullptr;
    }
}

// DCI delivers [length(2 LE)][event entries] (cmd 0x60 stripped). Per event:
//   _eid(2 LE): id=_eid&0xFFF, payload_ind=(_eid>>13)&3, ts_trunc=(_eid>>15)&1
//   timestamp: 8 bytes (ts_trunc=0) or 2 bytes (ts_trunc=1)
//   payload: 0/1/2 bytes, or [len][len bytes] when payload_ind==3.
bool QualcommLogParser::on_event(const uint8_t *buf, size_t len)
{
    if (!buf || len < 4)
        return false;
    size_t pos = 2; // skip 2-byte length
    uint16_t eid = static_cast<uint16_t>(buf[pos] | (buf[pos + 1] << 8));
    uint16_t event_id = eid & 0x0FFF;
    uint8_t pl_ind = (eid >> 13) & 0x3;
    uint8_t ts_trunc = (eid >> 15) & 0x1;
    pos += 2;
    pos += ts_trunc ? 2 : 8;
    if (pos > len)
        return false;
    const uint8_t *pay = buf + pos;
    size_t paylen = 0;
    if (pl_ind == 1)
        paylen = 1;
    else if (pl_ind == 2)
        paylen = 2;
    else if (pl_ind == 3 && pos < len)
    {
        paylen = buf[pos];
        pay = buf + pos + 1;
    }
    uint16_t arfcn = 0;
    if (event_id == 500 && paylen >= 2 && pay + 2 <= buf + len)
    {
        arfcn = static_cast<uint16_t>((pay[0] | (pay[1] << 8)) & 0x3FF);
        if (arfcn)
            gsm_.set_surround_arfcn_hint(arfcn);
    }
    const char *nm = qcp_event_name(event_id);
    DIAG_LOGD("QCP: event id=%u (%s) arfcn=%u", event_id, nm ? nm : "?", arfcn);
    if (journal_enabled())
    {
        char sm[96];
        if (arfcn)
            std::snprintf(sm, sizeof(sm), "%s arfcn=%u", nm ? nm : "event", arfcn);
        else
            std::snprintf(sm, sizeof(sm), "event_id=%u%s", event_id, nm ? "" : " unknown");
        JournalRecord jr;
        jr.t = static_cast<double>(time(nullptr));
        jr.code = 0;
        jr.rat = "EVENT";
        jr.channel = "EVENT";
        jr.msg_type = nm ? nm : "EVENT";
        jr.summary = sm;
        jr.raw = journal_hex(buf, len);
        jr.len = len;
        journal_emit(jr);
    }
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// dump_raw_first_seen — one-time RAW hex dump per (log_code, payload_version).
//
// This is the project's "never invent an offset" methodology generalised to
// EVERY RAT. On the first packet of each distinct (code, version) it prints the
// full payload to logcat (tag DiagParser), so a single scan run reveals exactly
// which packet versions the SM8550 modem emits and their byte layout for
// GSM / UMTS / WCDMA / LTE / NR. Capture with:
//     adb logcat -s DiagParser:I | grep RAWCAP
// then the per-version struct layouts can be ported from scat precisely.
//
// Negligible overhead: one dump per distinct key for the whole process lifetime.
// Set env OBSERVER_RAWDUMP=0 to disable.
// ═════════════════════════════════════════════════════════════════════════════
static void dump_raw_first_seen(uint16_t code, const uint8_t *payload, size_t plen)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char *e = getenv("OBSERVER_RAWDUMP");
        enabled = (e && e[0] == '0') ? 0 : 1;
    }
    if (!enabled || !payload || plen == 0)
        return;

    // version discriminator: most Qualcomm log payloads start with a 1-byte
    // version (LTE ML1, NR, WCDMA-resel use byte[0]; some use a packed nibble).
    uint8_t ver = payload[0];
    static std::set<uint32_t> seen;
    uint32_t key = (uint32_t(code) << 8) | ver;
    if (!seen.insert(key).second)
        return; // already dumped this (code,ver)

    DIAG_LOGI("RAWCAP code=0x%04X ver=%u plen=%zu  (%s)",
              code, ver, plen, QualcommLogParser::log_code_name(code));

    // chunked hex so logcat doesn't truncate; 32 bytes/line with byte offset
    char line[8 + 32 * 3 + 1];
    for (size_t off = 0; off < plen; off += 32)
    {
        size_t n = (plen - off < 32) ? (plen - off) : 32;
        int o = std::snprintf(line, sizeof(line), "+%03zu ", off);
        for (size_t k = 0; k < n && o + 3 < (int)sizeof(line); ++k)
            o += std::snprintf(line + o, sizeof(line) - o, "%02X ", payload[off + k]);
        DIAG_LOGI("RAWCAP 0x%04X v%u %s", code, ver, line);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// dispatch_log — internal routing by log code
// ═════════════════════════════════════════════════════════════════════════════
bool QualcommLogParser::dispatch_log(uint16_t code,
                                     const uint8_t *buf, size_t len,
                                     uint64_t /*ts*/)
{
    auto it = code_to_rat_.find(code);
    if (it == code_to_rat_.end())
    {
        DIAG_LOGD("QCP: unhandled log code 0x%04X", code);
        return false;
    }

    // One-time RAW capture per (code, version) across all RATs (see above).
    if (len > sizeof(LogRecord))
        dump_raw_first_seen(code, buf + sizeof(LogRecord), len - sizeof(LogRecord));

    switch (it->second)
    {
    case Rat::GSM:
        return gsm_.parse(buf, len);
    case Rat::WCDMA:
        return wcdma_.parse(buf, len);
    case Rat::LTE_RRC:
        return lte_rrc_.parse(buf, len); // ★
    case Rat::LTE_NAS:
        return lte_nas_.parse(buf, len);
    case Rat::LTE:
        return lte_ml1_.parse(buf, len);
    case Rat::UMTS:
        return umts_.parse(buf, len);
    case Rat::NR:
        return nr_.parse(buf, len);
    default:
        return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// merge_lte_identity — cross-reference RRC ↔ ML1
//
// Source-of-truth hierarchy for LTE serving cell:
//   1. RRC (0xB0C2) — full identity (PCI/EARFCN/CID/TAC/MCC/MNC/Band/BW)
//   2. NAS (0xB0EC) — fallback MCC/MNC/TAC from Attach Accept
//   3. ML1 (0xB193) — measurements only (RSRP/RSRQ/RSSI/SINR)
//
// Match ML1 measurement cells with RRC identity by EARFCN+PCI. Cells that
// match get serving=true and full identity fields. Cells that don't match
// remain measurement-only.
// ═════════════════════════════════════════════════════════════════════════════
void QualcommLogParser::merge_lte_identity()
{
    const LteCellIdentity &rrc = lte_rrc_.identity();
    const LteCellIdentity &nas = lte_nas_.identity();

    // We need mutable access to ML1 neighbors list. The parser exposes
    // a const accessor — cast away const for this internal merge.
    auto &neighbors = const_cast<std::vector<LteCell> &>(lte_ml1_.neighbors());

    // ── 1) Drain SIB1-decoded foreign-PLMN cells from the RRC parser ────
    auto &sib1_cells = lte_rrc_.sib1_cells();
    for (const auto &sc : sib1_cells)
    {
        bool merged = false;
        for (auto &n : neighbors)
        {
            if (n.earfcn == sc.earfcn && n.pci == sc.pci)
            {
                if (n.cell_id <= 0)
                    n.cell_id = sc.cell_id;
                if (n.tac == 0)
                    n.tac = sc.tac;
                if (n.mcc == 0)
                    n.mcc = sc.mcc;
                if (n.mnc == 0)
                    n.mnc = sc.mnc;
                merged = true;
                break;
            }
        }
        if (!merged)
        {
            neighbors.push_back(sc);
        }
    }
    sib1_cells.clear(); // drained — don't re-merge next cycle

    // ── 1a) Drain accumulated B0C2 serving-cell identities ─────────────
    auto &serv_cells = lte_rrc_.serv_cells();
    for (const auto &sc : serv_cells)
    {
        bool merged = false;
        for (auto &n : neighbors)
        {
            if (n.earfcn == sc.earfcn && n.pci == sc.pci)
            {
                if (n.cell_id <= 0)
                    n.cell_id = sc.cell_id;
                if (n.tac == 0)
                    n.tac = sc.tac;
                if (n.mcc == 0)
                    n.mcc = sc.mcc;
                if (n.mnc == 0)
                    n.mnc = sc.mnc;
                if (n.dl_bw == 0)
                    n.dl_bw = sc.dl_bw;
                if (n.ul_bw == 0)
                    n.ul_bw = sc.ul_bw;
                merged = true;
                break;
            }
        }
        if (!merged)
            neighbors.push_back(sc);
    }
    serv_cells.clear(); // drained

    // ── 1b) Apply UL-DCCH MeasurementReport rsrp/rsrq ──────────────────
    auto &meas = lte_rrc_.meas_reports();
    for (const auto &m : meas)
    {
        for (auto &n : neighbors)
        {
            if (n.earfcn != m.earfcn || n.pci != m.pci)
                continue;
            if (n.rsrp == 0 && m.rsrp != 0)
                n.rsrp = m.rsrp;
            if (n.rsrq == 0 && m.rsrq != 0)
                n.rsrq = m.rsrq;
        }
    }
    meas.clear(); // drained

    // ─────────────────────────────────────────────────────────────────────────
    // НАЧАЛО СЕНЬОРСКОГО ПАТЧА ДЛЯ QUALCOMM X55 / SIM8300
    // ─────────────────────────────────────────────────────────────────────────
    // Если глобальные статусы rrc/nas не валидны (потому что 0xB0C2 не прилетел),
    // но в списке neighbors лежит сота, только что вытащенная из SIB1 (0xB0C0),
    // мы принудительно объявляем её служебной (SERVING), чтобы завести Live Monitor!
    if (!rrc.valid && !nas.valid)
    {
        for (auto &c : neighbors)
        {
            if (c.cell_id > 0 && !c.serving)
            {
                c.serving = true; // Сигнализируем коллбэку, что это Serving сота!
                break;
            }
        }

        // Стреляем обновлением в main_linux.cpp, чтобы отработал set_neighbor_callback
        fire_neighbor_update();
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────
    // КОНЕЦ СЕНЬОРСКОГО ПАТЧА
    // ─────────────────────────────────────────────────────────────────────────

    // Каноничный цикл автора (вызовется, если rrc.valid или nas.valid проснутся)
    for (auto &c : neighbors)
    {
        c.serving = false;

        if (rrc.valid && c.earfcn == rrc.earfcn && c.pci == rrc.pci)
        {
            c.serving = true;
            c.mcc = rrc.mcc;
            c.mnc = rrc.mnc;
            c.cell_id = static_cast<int32_t>(rrc.cell_id);
            c.tac = rrc.tac;
        }
    }

    fire_neighbor_update();
}

// ═════════════════════════════════════════════════════════════════════════════
// fire_neighbor_update — assemble a ParsedNeighbors snapshot
// ═════════════════════════════════════════════════════════════════════════════
void QualcommLogParser::fire_neighbor_update()
{
    if (!neighbor_cb_)
        return;

    ParsedNeighbors pn;

    // ── GSM ──
    if (gsm_.serving_cell().arfcn != 0)
        pn.gsm.push_back(gsm_.serving_cell());
    for (const auto &c : gsm_.neighbors())
        pn.gsm.push_back(c);

    // ── WCDMA ──
    if (wcdma_.serving_cell().uarfcn != 0)
        pn.wcdma.push_back(wcdma_.serving_cell());
    for (const auto &c : wcdma_.neighbors())
        pn.wcdma.push_back(c);

    // ── LTE: prefer ML1 measurements that have been merged with RRC identity ──
    // The merge_lte_identity() pass marks one cell as serving=true and
    // fills its MCC/MNC/CID/TAC fields. The rest are pure measurement.
    for (const auto &c : lte_ml1_.neighbors())
        pn.lte.push_back(c);

    // If we have RRC identity but no matching ML1 cell yet (e.g. first
    // packet of session) — emit a serving entry from RRC alone:
    const auto &rrc = lte_rrc_.identity();
    if (rrc.valid)
    {
        bool already = false;
        for (auto &c : pn.lte)
        {
            if (c.earfcn == rrc.earfcn && c.pci == rrc.pci && c.serving)
            {
                if (c.dl_bw == 0)
                    c.dl_bw = rrc.dl_bw;
                if (c.ul_bw == 0)
                    c.ul_bw = rrc.ul_bw;
                already = true;
                break;
            }
        }
        if (!already)
        {
            LteCell c{};
            c.earfcn = rrc.earfcn;
            c.pci = rrc.pci;
            c.mcc = rrc.mcc;
            c.mnc = rrc.mnc;
            c.cell_id = static_cast<int32_t>(rrc.cell_id);
            c.tac = rrc.tac;
            c.dl_bw = rrc.dl_bw;
            c.ul_bw = rrc.ul_bw;
            c.serving = true;
            pn.lte.push_back(c);
        }
    }

    // ── NR ──
    if (nr_.serving_cell().nrarfcn != 0)
        pn.nr.push_back(nr_.serving_cell());
    for (const auto &c : nr_.neighbors())
        pn.nr.push_back(c);

    neighbor_cb_(pn);
}

// ═════════════════════════════════════════════════════════════════════════════
// Accessors
// ═════════════════════════════════════════════════════════════════════════════
const GsmCell &QualcommLogParser::gsm_serving() const { return gsm_.serving_cell(); }
const WcdmaCell &QualcommLogParser::wcdma_serving() const { return wcdma_.serving_cell(); }
const LteCell &QualcommLogParser::lte_serving() const { return lte_ml1_.serving_cell(); }
const NrCell &QualcommLogParser::nr_serving() const { return nr_.serving_cell(); }
const std::vector<GsmCell> &QualcommLogParser::gsm_neighbors() const { return gsm_.neighbors(); }
const std::vector<WcdmaCell> &QualcommLogParser::wcdma_neighbors() const { return wcdma_.neighbors(); }
const std::vector<LteCell> &QualcommLogParser::lte_neighbors() const { return lte_ml1_.neighbors(); }
const std::vector<NrCell> &QualcommLogParser::nr_neighbors() const { return nr_.neighbors(); }

// ═════════════════════════════════════════════════════════════════════════════
// Static helpers
// ═════════════════════════════════════════════════════════════════════════════
std::vector<uint16_t> QualcommLogParser::all_log_codes()
{
    std::vector<uint16_t> codes;
    auto append = [&](std::vector<uint16_t> v)
    {
        codes.insert(codes.end(), v.begin(), v.end());
    };
    append(DiagGsmLogParser::handled_log_codes());
    append(DiagWcdmaLogParser::handled_log_codes());
    append(DiagLteRrcParser::handled_log_codes());
    append(DiagLteNasParser::handled_log_codes());
    append(DiagLteMl1Parser::handled_log_codes());
    append(DiagUmtsLogParser::handled_log_codes());
    append(DiagNrLogParser::handled_log_codes());
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

Rat QualcommLogParser::log_code_rat(uint16_t code)
{
    if (code >= LOG_WCDMA_BASE_C && code < LOG_GSM_BASE_C)
        return Rat::WCDMA;
    if (code >= LOG_GSM_BASE_C && code < 0x6000)
        return Rat::GSM;
    if (code >= LOG_UMTS_BASE_C && code < 0x8000)
        return Rat::UMTS;
    // LTE codes split by sub-range:
    if (code == 0xB0C0 || code == 0xB0C1 || code == 0xB0C2)
        return Rat::LTE_RRC;
    if (code >= 0xB0E0 && code <= 0xB0ED)
        return Rat::LTE_NAS;
    if (code >= LOG_LTE_BASE_C && code <= LOG_LTE_LAST_C)
        return Rat::LTE;
    if (code >= LOG_NR_BASE_C && code <= LOG_NR_LAST_C)
        return Rat::NR;
    return Rat::UNKNOWN;
}

const char *QualcommLogParser::log_code_name(uint16_t code)
{
    switch (code)
    {
    // GSM (constant names per the new diag_gsm_log_parser.h)
    case LOG_GSM_CELL_INFO:
        return "GSM RR Cell Information";
    case LOG_GSM_L1_SURROUND_CELL_BA_LIST_C:
        return "GSM Surround BA List";
    case LOG_GSM_L1_NEIG_AUX_MEAS:
        return "GSM Neighbor Aux Meas";
    case LOG_GSM_L1_SERV_AUX_MEAS:
        return "GSM Serving Aux Meas";
    case LOG_GSM_L1_BURST_METRICS_C:
        return "GSM L1 Burst Metrics";
    case LOG_GSM_GSM_RR:
        return "GSM RR Signaling";
    case LOG_GSM_L1_NEW_BURST_METRICS_C:
        return "GSM L1 New Burst Metrics";
    case 0x5075:
        return "GSM Neighbor Cell Acq";
    case 0x5A75:
        return "GSM DSDS Neighbor Cell Acq";
    case LOG_GSM_DSDS_L1_BURST_METRIC:
        return "GSM DSDS Burst Metrics";
    case LOG_GSM_DSDS_L1_SURROUND_CELL_BA:
        return "GSM DSDS Surround BA List";
    case LOG_GSM_DSDS_L1_SERV_AUX_MEAS:
        return "GSM DSDS Serving Aux Meas";
    case LOG_GSM_DSDS_L1_NEIG_AUX_MEAS:
        return "GSM DSDS Neighbor Aux Meas";
    case LOG_GSM_DSDS_CELL_INFO:
        return "GSM DSDS Cell Information";
    case LOG_GSM_DSDS_RR:
        return "GSM DSDS RR Signaling";
    case LOG_GPRS_OTA:
        return "GPRS SM/GMM OTA";

    // WCDMA
    case LOG_WCDMA_NEIGHBOR_SET_C:
        return "WCDMA Neighbor Set";
    case LOG_WCDMA_SERVING_CELL_INFO_C:
        return "WCDMA Serving Cell Info";
    case LOG_WCDMA_SEARCH_CELL_RESELECTION_RANK_C:
        return "WCDMA Resel Rank";
    case LOG_WCDMA_CELL_ID_C:
        return "WCDMA Cell ID";

    // LTE RRC
    case LOG_LTE_RRC_OTA_C:
        return "LTE RRC OTA";
    case LOG_LTE_RRC_MIB_C:
        return "LTE RRC MIB";
    case LOG_LTE_RRC_SERV_CELL_INFO_C:
        return "LTE RRC Serving Cell Info";

    // LTE NAS
    case LOG_LTE_NAS_EMM_PLAIN_OTA_IN_C:
        return "LTE NAS EMM Plain OTA In";
    case LOG_LTE_NAS_EMM_PLAIN_OTA_OUT_C:
        return "LTE NAS EMM Plain OTA Out";
    case LOG_LTE_NAS_EMM_SEC_OTA_IN_C:
        return "LTE NAS EMM Sec OTA In";

    // LTE ML1
    case LOG_LTE_ML1_SERVING_MEAS_EVAL_C:
        return "LTE ML1 Serving Meas & Eval";
    case LOG_LTE_ML1_NEIGHBOR_MEAS_C:
        return "LTE ML1 Neighbor Meas";
    case LOG_LTE_ML1_SCELL_MEAS_RESP_C:
        return "LTE ML1 SCell Meas Response";
    case LOG_LTE_ML1_SERVING_CELL_INFO_C:
        return "LTE ML1 Serving Cell Info";

    // UMTS
    case LOG_UMTS_NAS_OTA_C:
        return "UMTS NAS OTA";
    case LOG_UMTS_NAS_GMMSM_OTA_C:
        return "UMTS GMM/SM OTA";
    case LOG_UMTS_NAS_MM_STATE_C:
        return "UMTS MM State";
    case LOG_UMTS_RRC_OTA_C:
        return "UMTS RRC OTA";

    // NR
    case LOG_NR5G_ML1_MEAS_DB_UPDATE_C:
        return "NR5G ML1 Meas DB Update";
    case LOG_NR5G_ML1_SERVING_CELL_MEAS_C:
        return "NR5G ML1 Serving Cell Meas";

    default:
        return "Unknown";
    }
}
