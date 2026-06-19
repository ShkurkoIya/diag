#include "diag_dci_client.h"
#include "diag_common.h"
#include "event_ids.h"

#include <cstring>
#include <algorithm>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Предустановленный список event ID, полезных для сбора соседей
// ─────────────────────────────────────────────────────────────────────────────
static const uint32_t kDefaultEventIds[] = {
        // LTE RRC
        EVENT_LTE_RRC_STATE_CHANGE,
        EVENT_LTE_RRC_NEW_CELL_IND,
        EVENT_LTE_RRC_IRAT_HO_FROM_EUTRAN,
        EVENT_LTE_RRC_IRAT_RESEL_FROM_EUTRAN,
        EVENT_LTE_RRC_CELL_RESEL_FAILURE,
        EVENT_LTE_ML1_STATE_CHANGE,
        // WCDMA
        EVENT_WCDMA_L1_STATE,
        EVENT_WCDMA_ASET,
        EVENT_WCDMA_NEW_REFERENCE_CELL,
        EVENT_WCDMA_TO_WCDMA_RESELECTION_START,
        EVENT_WCDMA_TO_WCDMA_RESELECTION_END,
        EVENT_WCDMA_TO_GSM_RESELECTION_START,
        EVENT_WCDMA_TO_GSM_RESELECTION_END,
        EVENT_WCDMA_INTER_RAT_HANDOVER_START,
        EVENT_WCDMA_INTER_RAT_HANDOVER_END,
        // GSM
        EVENT_GSM_L1_STATE,
        EVENT_GSM_HANDOVER_START,
        EVENT_GSM_HANDOVER_END,
        EVENT_GSM_RESELECT_START,
        EVENT_GSM_RESELECT_END,
        EVENT_GSM_CELL_SELECTION_START,
        EVENT_GSM_CAMP_ATTEMPT_START,
        EVENT_GSM_RR_IN_SERVICE,
};
static const int kDefaultEventIdsCount =
        static_cast<int>(sizeof(kDefaultEventIds) / sizeof(kDefaultEventIds[0]));

// ─────────────────────────────────────────────────────────────────────────────
DiagDciClient::DiagDciClient() {
    // Предустановленные маски
    log_codes_ = QualcommLogParser::all_log_codes();
    event_ids_.assign(kDefaultEventIds, kDefaultEventIds + kDefaultEventIdsCount);
}

DiagDciClient::~DiagDciClient() {
    if (state_ == State::Running)
        stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Публичные конфигураторы
// ─────────────────────────────────────────────────────────────────────────────
void DiagDciClient::set_neighbor_callback(NeighborCallback cb) {
    parser_.set_neighbor_callback(std::move(cb));
}
void DiagDciClient::set_raw_frame_callback(RawFrameCallback cb) {
    parser_.set_raw_frame_callback(std::move(cb));
}
void DiagDciClient::set_nas_callback(NasCallback cb) {
    parser_.set_nas_callback(std::move(cb));
}
void DiagDciClient::set_rrc_callback(RrcCallback cb) {
    parser_.set_rrc_callback(std::move(cb));
}
void DiagDciClient::set_log_codes(std::vector<uint16_t> codes) {
    log_codes_ = std::move(codes);
}
void DiagDciClient::set_event_ids(std::vector<uint32_t> ids) {
    event_ids_ = std::move(ids);
}

// ─────────────────────────────────────────────────────────────────────────────
// start() – главная точка входа
// ─────────────────────────────────────────────────────────────────────────────
bool DiagDciClient::start() {
    if (state_ == State::Running) {
        DIAG_LOGW("DiagDciClient: already running");
        return true;
    }

    DIAG_LOGI("DiagDciClient: starting...");

    // Каждый шаг логирует свою ошибку сам
    if (!step_load_library()    ) { state_ = State::Error; return false; }
    if (!step_lsm_init()        ) { state_ = State::Error; return false; }
    if (!step_allocate_client()     ) { state_ = State::Error; step_lsm_deinit(); return false; }
    if (!step_register_callbacks()  ) { state_ = State::Error; step_release_client(); step_lsm_deinit(); return false; }
    if (!step_enable_log_mask() ) { state_ = State::Error; step_release_client(); step_lsm_deinit(); return false; }
    if (!step_enable_event_mask()) {
        // Ошибка маски событий некритична – продолжаем с только логами
        DIAG_LOGW("DiagDciClient: event mask failed, continuing with logs only");
    }

    state_ = State::Running;
    DIAG_LOGI("DiagDciClient: running (client_id=%d, log_codes=%zu, event_ids=%zu)",
              client_id_,
              log_codes_.size(),
              event_ids_.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// stop()
// ─────────────────────────────────────────────────────────────────────────────
void DiagDciClient::stop() {
    if (state_ != State::Running) return;

    DIAG_LOGI("DiagDciClient: stopping (client_id=%d)...", client_id_);

    step_disable_masks();
    step_release_client();
    step_lsm_deinit();
    LibdiagLoader::instance().unload();

    state_ = State::Stopped;
    DIAG_LOGI("DiagDciClient: stopped. Stats: logs=%llu events=%llu parsed=%llu errors=%llu",
              (unsigned long long)stats_.log_packets_received,
              (unsigned long long)stats_.event_packets_received,
              (unsigned long long)stats_.log_packets_parsed,
              (unsigned long long)stats_.parse_errors);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Шаги инициализации ───────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

bool DiagDciClient::step_load_library() {
    LibdiagLoader& loader = LibdiagLoader::instance();
    if (loader.is_loaded()) return true;

    if (!loader.load()) {
        DIAG_LOGE("DiagDciClient: libdiag.so not found or missing symbols");
        return false;
    }
    DIAG_LOGI("DiagDciClient: libdiag loaded from %s", loader.loaded_path().c_str());
    return true;
}

bool DiagDciClient::step_lsm_init() {
    LibdiagLoader& L = LibdiagLoader::instance();

    // Diag_LSM_Init(nullptr) – стандартный вызов без среды
    boolean ok = L.lsm_init(nullptr);
    if (!ok) {
        DIAG_LOGE("DiagDciClient: Diag_LSM_Init() failed");
        return false;
    }
    lsm_inited_ = true;
    DIAG_LOGD("DiagDciClient: Diag_LSM_Init() OK");
    return true;
}

bool DiagDciClient::step_allocate_client() {
    LibdiagLoader& L = LibdiagLoader::instance();

    if (L.variant() == LibdiagLoader::ApiVariant::VariantA) {
        // Старый API: int diag_allocate_client(void)
        int id = L.allocate_client();
        if (id < 0) {
            DIAG_LOGE("DiagDciClient: diag_allocate_client() returned %d", id);
            return false;
        }
        client_id_ = id;

    } else if (L.variant() == LibdiagLoader::ApiVariant::VariantB) {

    /* Initialization function required for DCI functions. Input parameters are:
       a) pointer to an int which holds client id
       b) pointer to a bit mask which holds peripheral information,
       c) an integer to specify which processor to talk to (Local or Remote in the case of Fusion Devices),
       d) void* for future needs (not implemented as of now) */
        diag_dci_peripherals list = DIAG_CON_MPSS | DIAG_CON_APSS | DIAG_CON_LPASS;
        static int channel = MSM;
        int signal_type = SIGCONT;
        /* Signal handling to handle SSR */
        struct sigaction notify_action;
        sigemptyset(&notify_action.sa_mask);
        notify_action.sa_sigaction = notify_handler;
        /* Use SA_SIGINFO to denote we are expecting data with the signal */
        notify_action.sa_flags = SA_SIGINFO;
        sigaction(signal_type, &notify_action, NULL);
        int ret = L.register_dci_client(&client_id_, &list, channel, &signal_type);
        if (ret != DIAG_DCI_NO_ERROR) {
            DIAG_LOGE("DiagDciClient: diag_register_dci_client() returned %d (client_id=%d)",
                      ret, client_id_);
            return false;
        }

    } else {
        DIAG_LOGE("DiagDciClient: unknown API variant");
        return false;
    }

    DIAG_LOGI("DiagDciClient: allocated client_id=%d (variant %s)",
              client_id_,
              L.variant() == LibdiagLoader::ApiVariant::VariantA ? "A" : "B");
    return true;
}
void DiagDciClient::notify_handler(int signal, siginfo_t *info, void *unused)
{
    (void)unused;

    if (info) {
        int err;
        diag_dci_peripherals list = 0;

        DIAG_LOGE("diag: In %s, signal %d received from kernel, data is: %x\n",
                  __func__, signal, info->si_int);

        if (info->si_int & DIAG_STATUS_OPEN) {
            if (info->si_int & DIAG_CON_MPSS) {
                DIAG_LOGE("diag: DIAG_STATUS_OPEN on DIAG_CON_MPSS\n");
            } else if (info->si_int & DIAG_CON_LPASS) {
                DIAG_LOGE("diag: DIAG_STATUS_OPEN on DIAG_CON_LPASS\n");
            } else {
                DIAG_LOGE("diag: DIAG_STATUS_OPEN on unknown peripheral\n");
            }
        } else if (info->si_int & DIAG_STATUS_CLOSED) {
            if (info->si_int & DIAG_CON_MPSS) {
                DIAG_LOGE("diag: DIAG_STATUS_CLOSED on DIAG_CON_MPSS\n");
            } else if (info->si_int & DIAG_CON_LPASS) {
                DIAG_LOGE("diag: DIAG_STATUS_CLOSED on DIAG_CON_LPASS\n");
            } else {
                DIAG_LOGE("diag: DIAG_STATUS_CLOSED on unknown peripheral\n");
            }
        }
        LibdiagLoader& loader = LibdiagLoader::instance();
        err = loader.diag_get_dci_support_list_proc(MSM, &list);
        if (err != DIAG_DCI_NO_ERROR) {
            DIAG_LOGE("diag: could not get support list, err: %d\n", err);
        }
        /* This will print out all peripherals supporting DCI */
        if (list & DIAG_CON_MPSS)
            DIAG_LOGE("diag: Modem supports DCI\n");
        if (list & DIAG_CON_LPASS)
            DIAG_LOGE("diag: LPASS supports DCI\n");
        if (list & DIAG_CON_WCNSS)
            DIAG_LOGE("diag: RIVA supports DCI\n");
        if (list & DIAG_CON_APSS)
            DIAG_LOGE("diag: APSS supports DCI\n");
        if (!list)
            DIAG_LOGE("diag: No current dci support\n");
    } else {
        DIAG_LOGE("diag: In %s, signal %d received from kernel, but no info value, info: 0x%p\n",
                  __func__, signal, info);
    }
}
// ─────────────────────────────────────────────────────────────────────────────
// ВАЖНО: diag_register_dci_stream_proc не принимает context-указатель,
// поэтому используем глобальный синглтон.  Устанавливаем его ДО регистрации.
// ─────────────────────────────────────────────────────────────────────────────
static DiagDciClient* g_dci_client_instance = nullptr;

bool DiagDciClient::step_register_callbacks() {
    LibdiagLoader& L = LibdiagLoader::instance();

    // Глобальный указатель должен быть установлен до того, как libdiag
    // начнёт вызывать callback-и (что может произойти сразу после возврата)
    g_dci_client_instance = this;

    int err = L.register_dci_stream(
            client_id_,
            &DiagDciClient::s_log_callback,
            &DiagDciClient::s_event_callback
    );

    // libdiag возвращает DIAG_DCI_NO_ERROR=1 при успехе
    if (err != DIAG_DCI_NO_ERROR) {
        g_dci_client_instance = nullptr;
        DIAG_LOGE("DiagDciClient: diag_register_dci_stream_proc() returned %d", err);
        return false;
    }
    DIAG_LOGD("DiagDciClient: DCI stream callbacks registered");
    return true;
}

bool DiagDciClient::step_enable_log_mask() {
    if (log_codes_.empty()) {
        DIAG_LOGW("DiagDciClient: log_codes list is empty, skipping log mask");
        return true;
    }

    LibdiagLoader& L = LibdiagLoader::instance();

    // diag_log_stream_config принимает uint16_t*, поэтому передаём напрямую
    int err = L.log_stream_config(
            client_id_,
            DCI_ENABLE,
            log_codes_.data(),
            static_cast<int>(log_codes_.size())
    );

    if (err != DIAG_DCI_NO_ERROR) {
        DIAG_LOGE("DiagDciClient: diag_log_stream_config(ENABLE) returned %d", err);
        return false;
    }
    DIAG_LOGI("DiagDciClient: log mask enabled (%zu codes)", log_codes_.size());

    // ── Verify critical RRC/NAS codes are in the mask ──────────────────
    // If you don't see "LTE RRC SCell v3:" logs during scan, check here:
    //   - 0xB0C0 = RRC OTA wrapper (MIB on SM8550)
    //   - 0xB0C1 = MIB pre-decoded
    //   - 0xB0C2 = RRC Serving Cell Info ★ (MCC/MNC/CID/TAC)
    //   - 0xB0EC = NAS Attach Accept (fallback identity)
    bool has_b0c0 = false, has_b0c1 = false, has_b0c2 = false, has_b0ec = false;
    for (uint16_t c : log_codes_) {
        if (c == 0xB0C0) has_b0c0 = true;
        if (c == 0xB0C1) has_b0c1 = true;
        if (c == 0xB0C2) has_b0c2 = true;
        if (c == 0xB0EC) has_b0ec = true;
    }
    DIAG_LOGI("DiagDciClient: mask contains B0C0=%d B0C1=%d B0C2=%d B0EC=%d",
              has_b0c0, has_b0c1, has_b0c2, has_b0ec);
    if (!has_b0c2) {
        DIAG_LOGW("DiagDciClient: 0xB0C2 NOT in mask — cell identity (MCC/MNC/CID/TAC) "
                  "won't be available. Check DiagLteRrcParser::handled_log_codes()");
    }
    return true;
}

bool DiagDciClient::step_enable_event_mask() {
    if (event_ids_.empty()) return true;

    LibdiagLoader& L = LibdiagLoader::instance();

    int err = L.event_stream_config(
            client_id_,
            DCI_ENABLE,
            event_ids_.data(),
            static_cast<int>(event_ids_.size())
    );

    if (err != DIAG_DCI_NO_ERROR) {
        DIAG_LOGW("DiagDciClient: diag_event_stream_config(ENABLE) returned %d", err);
        return false;
    }
    DIAG_LOGI("DiagDciClient: event mask enabled (%zu ids)", event_ids_.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Шаги деинициализации ─────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

void DiagDciClient::step_disable_masks() {
    LibdiagLoader& L = LibdiagLoader::instance();
    if (!L.is_loaded()) return;

    if (!log_codes_.empty() && L.log_stream_config) {
        L.log_stream_config(client_id_, DCI_DISABLE, log_codes_.data(),
                            static_cast<int>(log_codes_.size()));
        DIAG_LOGD("DiagDciClient: log mask disabled");
    }
    if (!event_ids_.empty() && L.event_stream_config) {
        L.event_stream_config(client_id_, DCI_DISABLE, event_ids_.data(),
                              static_cast<int>(event_ids_.size()));
        DIAG_LOGD("DiagDciClient: event mask disabled");
    }
}

void DiagDciClient::step_release_client() {
    if (client_id_ < 0) return;
    LibdiagLoader& L = LibdiagLoader::instance();
    if (!L.is_loaded()) return;

    if (L.variant() == LibdiagLoader::ApiVariant::VariantA) {
        if (L.release_client)
            L.release_client(client_id_);
    } else if (L.variant() == LibdiagLoader::ApiVariant::VariantB) {
        if (L.release_dci_client)
            L.release_dci_client(&client_id_);
    }

    DIAG_LOGD("DiagDciClient: released client_id=%d", client_id_);
    client_id_ = -1;
    g_dci_client_instance = nullptr;
}

void DiagDciClient::step_lsm_deinit() {
    if (!lsm_inited_) return;
    LibdiagLoader& L = LibdiagLoader::instance();
    if (!L.is_loaded() || !L.lsm_deinit) return;

    L.lsm_deinit();
    lsm_inited_ = false;
    DIAG_LOGD("DiagDciClient: Diag_LSM_DeInit() done");
}

// ─────────────────────────────────────────────────────────────────────────────
// Статические мосты C → C++
//
// libdiag вызывает эти функции из своего внутреннего потока.
// Мы перенаправляем на g_dci_client_instance.
// ─────────────────────────────────────────────────────────────────────────────
// CM system-selection preference = ACTIVE SCAN trigger.
// DIAG_SUBSYS_CMD_F(0x4B) -> DIAG_SUBSYS_CM(0x0F):
//   CM_SYSTEM_SELECTION_PREFERENCE2          (cmd 16)
//   CM_SYSTEM_SELECTION_PREFERENCE2_PER_SUBS (cmd 43, as_id=0)
// mode_pref: 13=GSM_ONLY 14=WCDMA_ONLY 38=LTE_ONLY 2=AUTOMATIC(restore).
// A *_ONLY mode + BAND_PREF=ANY forces the modem to re-acquire across the
// whole band, so every detectable cell appears in the L1/RR logs we capture.
// mode_pref=2 (AUTOMATIC) restores normal multi-RAT operation - ALWAYS send
// it when the scan ends, or the modem stays single-RAT (no data/LTE).
// Layout (CM ICD 80-V1294-7 sec 3.38), little-endian:
//   4B 0F <cmd:2> [as_id:1] <net_sel:1> <PLMN:3> <mode:4> <acq:4>
//   <band:8> <roam:4> <hybr:4> <srv:4>
void DiagDciClient::send_syssel_pref(uint32_t mode_pref, const std::string& plmn_str) {
    LibdiagLoader& loader = LibdiagLoader::instance();
    if (!lsm_inited_)               { DIAG_LOGW("send_syssel_pref: LSM not inited"); return; }
    if (!loader.send_dci_async_req) { DIAG_LOGW("send_syssel_pref: send_dci_async_req unavailable"); return; }

    const bool     restore = (mode_pref == 2);          // AUTOMATIC
    const uint8_t  net_sel = restore ? 0 : 1;            // AUTO vs MANUAL
    uint8_t        plmn[3];
    if (restore) {
        plmn[0]=0x00; plmn[1]=0x00; plmn[2]=0x00;
    } else {
        // MANUAL target PLMN as 3-byte BCD (3GPP TS 24.008). Empty/short =>
        // 99999 (MCC=999/MNC=99, invalid => perpetual band search / scan-all).
        std::string pp = plmn_str;
        if (pp.size() < 5) pp = "99999";
        auto d = [](char c){ return (uint8_t)(c >= '0' && c <= '9' ? c - '0' : 0xF); };
        std::string mcc = pp.substr(0, 3);
        std::string mnc = pp.substr(3);
        plmn[0] = (uint8_t)((d(mcc[1]) << 4) | d(mcc[0]));
        if (mnc.size() >= 3) {
            plmn[1] = (uint8_t)((d(mnc[2]) << 4) | d(mcc[2]));
            plmn[2] = (uint8_t)((d(mnc[1]) << 4) | d(mnc[0]));
        } else {
            plmn[1] = (uint8_t)((0xF << 4) | d(mcc[2]));
            plmn[2] = (uint8_t)((d(mnc[1]) << 4) | d(mnc[0]));
        }
        DIAG_LOGI("send_syssel_pref: plmn=%s -> BCD %02X%02X%02X",
                  pp.c_str(), plmn[0], plmn[1], plmn[2]);
    }
    const uint32_t acq  = restore ? 0u : 3u;             // AUTO vs NO_CHANGE
    const uint64_t band = 0x3FFFFFFFull;                 // ANY (mode intersects)
    const uint32_t roam = restore ? 0xFFu : 0x100u;      // ANY vs NO_CHANGE
    const uint32_t hybr = 2u;                            // NO_CHANGE
    const uint32_t srv  = 4u;                            // NO_CHANGE

    auto build = [&](uint16_t cmd, bool per_subs) {
        std::vector<uint8_t> b;
        auto u16=[&](uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); };
        auto u32=[&](uint32_t v){ for(int i=0;i<4;++i) b.push_back((v>>(8*i))&0xFF); };
        auto u64=[&](uint64_t v){ for(int i=0;i<8;++i) b.push_back((uint8_t)((v>>(8*i))&0xFF)); };
        b.push_back(0x4B); b.push_back(0x0F); u16(cmd);
        if (per_subs) b.push_back(0x00);                 // as_id (primary sub)
        b.push_back(net_sel);
        b.push_back(plmn[0]); b.push_back(plmn[1]); b.push_back(plmn[2]);
        u32(mode_pref); u32(acq); u64(band); u32(roam); u32(hybr); u32(srv);
        return b;
    };

    uint8_t rsp[4096] = {};
    auto send_one = [&](uint16_t cmd, bool per_subs) {
        std::vector<uint8_t> pkt = build(cmd, per_subs);
        int ret = loader.send_dci_async_req(client_id_, pkt.data(),
                                            static_cast<int>(pkt.size()),
                                            rsp, sizeof(rsp), nullptr, this);
        DIAG_LOGI("send_syssel_pref: CM cmd=%u mode_pref=%u (%zu bytes) ret=%d",
                  cmd, mode_pref, pkt.size(), ret);
    };
    send_one(16, false);
    send_one(43, true);
}

void DiagDciClient::s_log_callback(unsigned char* buf, int len, void* /*ctx*/) {
    if (g_dci_client_instance && len > 0)
        g_dci_client_instance->on_log_packet(buf, static_cast<int>(len));
}

void DiagDciClient::s_event_callback(unsigned char* buf, int len, void* /*ctx*/) {
    if (g_dci_client_instance && len > 0)
        g_dci_client_instance->on_event_packet(buf, static_cast<int>(len));
}

// ─────────────────────────────────────────────────────────────────────────────
// Инстанс-обработчики
// ─────────────────────────────────────────────────────────────────────────────
void DiagDciClient::on_log_packet(unsigned char* buf, int len) {
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        ++stats_.log_packets_received;
    }

    bool ok = parser_.on_log(reinterpret_cast<uint8_t*>(buf),
                             static_cast<size_t>(len));
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        if (ok) ++stats_.log_packets_parsed;
        else    ++stats_.parse_errors;
    }
}

void DiagDciClient::on_event_packet(unsigned char* buf, int len) {
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        ++stats_.event_packets_received;
    }
    parser_.on_event(reinterpret_cast<uint8_t*>(buf),
                     static_cast<size_t>(len));
}

//void DiagDciClient::rebootModem() {
//    LibdiagLoader& loader = LibdiagLoader::instance();
//    if (!lsm_inited_) return;
//    if (!loader.send_dci_async_req) {
//        return;
//    }
//    std::vector<uint8_t> cmd_buf(5);
//    cmd_buf[0] = 0x4B;                           // DIAG_SUBSYS_CMD_F
//    cmd_buf[1] = 37;                           // DIAG_SUBSYS_DEBUG
//    cmd_buf[2] = 3; // TMS_DIAGPKT_ERR_CORE_DUMP
//    cmd_buf[3] = 0;
//    cmd_buf[4] = 1;
//
//    // Response buffer (also passed to DCI — some impls write here directly)
//    uint8_t rsp_raw[4096] = {};
//
//    // Send
//    int ret = loader.send_dci_async_req(
//            client_id_,
//            cmd_buf.data(),
//            static_cast<int>(cmd_buf.size()),
//            rsp_raw,
//            sizeof(rsp_raw),
//            nullptr,
//            this
//    );
//
//    fprintf(stderr, "  [REBOOT] send_dci_async_req returned %d (OK=%d)\n",
//                        ret, DIAG_DCI_NO_ERROR);
//
//    if (ret != DIAG_DCI_NO_ERROR) {
//        return;
//    }
//
//    return;
//}

void DiagDciClient::rebootModem() {
    LibdiagLoader& loader = LibdiagLoader::instance();
    if (!lsm_inited_) return;
    if (!loader.send_dci_async_req) {
        return;
    }

    // Modem crash reboot via DIAG_SUBSYS_DEBUG / TMS_DIAGPKT_ERR_CORE_DUMP
    //
    // FIX: Was 5 bytes [4B 25 03 00 01] with only ONE parameter (1).
    // The command requires BOTH parameters [0, 1]:
    //   ERR_INJECT_ERR_FATAL = 0
    //   ERR_INJECT_CRASH_DELAY_DIAG_DEFAULT = 1
    // Without the first 0 byte, the modem silently ignores the command,
    // which is why some lock writes "succeed" but never apply.
    //
    // Correct packet: [4B 25 03 00 00 01] — 6 bytes
    std::vector<uint8_t> cmd_buf(6);
    cmd_buf[0] = 0x4B;   // DIAG_SUBSYS_CMD_F
    cmd_buf[1] = 37;     // DIAG_SUBSYS_DEBUG
    cmd_buf[2] = 3;      // TMS_DIAGPKT_ERR_CORE_DUMP (LSB)
    cmd_buf[3] = 0;      // (MSB)
    cmd_buf[4] = 0;      // ERR_INJECT_ERR_FATAL
    cmd_buf[5] = 1;      // ERR_INJECT_CRASH_DELAY_DIAG_DEFAULT

    fprintf(stderr, "[REBOOT] Sending crash command: 4B 25 03 00 00 01 (6 bytes)\n");

    uint8_t rsp_raw[4096] = {};

    int ret = loader.send_dci_async_req(
            client_id_,
            cmd_buf.data(),
            static_cast<int>(cmd_buf.size()),
            rsp_raw,
            sizeof(rsp_raw),
            nullptr,
            this
    );

    fprintf(stderr, "[REBOOT] send_dci_async_req returned %d (OK=%d)\n",
            ret, DIAG_DCI_NO_ERROR);
}
