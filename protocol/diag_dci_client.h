#pragma once
#ifndef DIAG_DCI_CLIENT_H
#define DIAG_DCI_CLIENT_H

/*
 * diag_dci_client.h
 *
 * Полный жизненный цикл DCI-клиента поверх LibdiagLoader:
 *
 *   1. Диагностика загрузки libdiag.so
 *   2. Diag_LSM_Init()
 *   3. diag_allocate_client()
 *   4. diag_register_dci_stream_proc(client_id, log_cb, event_cb)
 *   5. diag_log_stream_config(client_id, ENABLE, codes, n)
 *   6. diag_event_stream_config(client_id, ENABLE, events, n)
 *   7. Приём пакетов → QualcommLogParser::on_log() / on_event()
 *   8. При остановке: DISABLE маски + diag_release_client() + Diag_LSM_DeInit()
 *
 * Использование:
 *
 *   DiagDciClient client;
 *   client.set_neighbor_callback([](const ParsedNeighbors& n){ ... });
 *
 *   if (!client.start()) { // ошибка }
 *   // ... работаем ...
 *   client.stop();
 */

#include "libdiag_loader.h"
#include "qualcomm_log_parser.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

typedef uint16_t diag_dci_peripherals;
#define DIAG_CON_APSS (0x0001)   /* Bit mask for APSS */
#define DIAG_CON_MPSS (0x0002)   /* Bit mask for MPSS */
#define DIAG_CON_LPASS (0x0004) /* Bit mask for LPASS */
#define DIAG_CON_WCNSS (0x0008) /* Bit mask for WCNSS */

#define DIAG_STATUS_OPEN (0x00010000)	/* Bit mask for DCI channel open status   */
#define DIAG_STATUS_CLOSED (0x00020000)	/* Bit mask for DCI channel closed status */

#define MSM	0
#define MDM	1

#define ENABLE			1
#define DISABLE			0
#define IN_BUF_SIZE		16384

#define DIAG_INVALID_SIGNAL	0

#define DIAG_PROC_MSM		0
#define DIAG_PROC_MDM		1

#define DIAG_ALL_PROC		-1
#define DIAG_MODEM_PROC		0
#define DIAG_LPASS_PROC		1
#define DIAG_WCNSS_PROC		2
#define DIAG_APPS_PROC		3

// ─────────────────────────────────────────────────────────────────────────────
// Статистика сессии – сколько пакетов прошло через клиент
// ─────────────────────────────────────────────────────────────────────────────
struct DiagDciStats {
    uint64_t log_packets_received   = 0;
    uint64_t event_packets_received = 0;
    uint64_t log_packets_parsed     = 0;
    uint64_t parse_errors           = 0;
};


// ─────────────────────────────────────────────────────────────────────────────
class DiagDciClient {
public:
    using NeighborCallback = QualcommLogParser::NeighborCallback;
    using RawFrameCallback = QualcommLogParser::RawFrameCallback;
    using NasCallback      = QualcommLogParser::NasCallback;
    using RrcCallback      = QualcommLogParser::RrcCallback;

    // Статусы, которые можно прочитать снаружи
    enum class State {
        Idle,        // ещё не запущен
        Running,     // активный DCI поток
        Stopped,     // штатная остановка
        Error,       // ошибка инициализации
    };

    DiagDciClient();
    ~DiagDciClient();

    // Disable copy
    DiagDciClient(const DiagDciClient&)            = delete;
    DiagDciClient& operator=(const DiagDciClient&) = delete;

    // ── Конфигурация (до start()) ────────────────────────────────────────────

    void set_neighbor_callback(NeighborCallback cb);
    void set_raw_frame_callback(RawFrameCallback cb);
    void set_nas_callback(NasCallback cb);
    void set_rrc_callback(RrcCallback cb);

    // Переопределить набор log codes (по умолчанию = QualcommLogParser::all_log_codes())
    void set_log_codes(std::vector<uint16_t> codes);

    // Переопределить набор event ids (по умолчанию = предустановленный список)
    void set_event_ids(std::vector<uint32_t> ids);


    static void notify_handler(int signal, siginfo_t *info, void *unused);

    // ── Жизненный цикл ───────────────────────────────────────────────────────

    // Загрузить libdiag, инициализировать DCI, включить маски.
    // Возвращает true при успехе.
    bool start();

    // Выключить маски, освободить клиент, выгрузить LSM.
    void stop();


    // ── Состояние ────────────────────────────────────────────────────────────

    State       state()     const { return state_.load(); }
    int         client_id() const { return client_id_; }
    const DiagDciStats& stats() const { return stats_; }

    // Доступ к парсеру для прямых запросов
    QualcommLogParser& parser() { return parser_; }


    void rebootModem();

    // Active-scan trigger: write CM system-selection preference
    // (mode_pref 13=GSM_ONLY,14=WCDMA_ONLY,38=LTE_ONLY,2=AUTOMATIC restore).
    void send_syssel_pref(uint32_t mode_pref, const std::string& plmn_str = "");

private:
    // ── Шаги инициализации ───────────────────────────────────────────────────
    bool step_load_library();
    bool step_lsm_init();
    bool step_allocate_client();
    bool step_register_callbacks();
    bool step_enable_log_mask();
    bool step_enable_event_mask();

    // ── Шаги деинициализации ────────────────────────────────────────────────
    void step_disable_masks();
    void step_release_client();
    void step_lsm_deinit();

    // ── Статические DCI callback-и (мост C → C++) ───────────────────────────
    // libdiag вызывает эти функции из своего потока
    static void s_log_callback  (unsigned char* buf, int len, void* ctx);
    static void s_event_callback(unsigned char* buf, int len, void* ctx);

    // Инстанс-обработчики
    void on_log_packet  (unsigned char* buf, int len);
    void on_event_packet(unsigned char* buf, int len);

    // ── Данные ───────────────────────────────────────────────────────────────
    std::atomic<State>  state_     { State::Idle };
    int                 client_id_ { -1 };
    bool                lsm_inited_{ false };

    QualcommLogParser   parser_;
    DiagDciStats        stats_;

    std::vector<uint16_t> log_codes_;
    std::vector<uint32_t> event_ids_;

    mutable std::mutex   stats_mutex_;
};

#endif // DIAG_DCI_CLIENT_H