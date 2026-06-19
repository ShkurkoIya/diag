#pragma once
#ifndef LIBDIAG_LOADER_H
#define LIBDIAG_LOADER_H

/*
 * libdiag_loader.h
 *
 * Динамически загружает libdiag.so и резолвит DCI API через dlopen/dlsym.
 *
 * Поддерживает два варианта ABI, которые встречаются в реальных устройствах:
 *
 * Вариант A (старый, из diag_lsm.h Leeco/AOSP):
 *   diag_allocate_client() → int
 *   diag_release_client(int)
 *
 * Вариант B (новый, из diag_lsm_dci.h CAF/Qualcomm vendor):
 *   diag_register_dci_client(int *client_id, void *tbl, int count, int data_type) → int
 *   diag_release_dci_client(int *client_id) → int
 *
 * Конкретная .so на устройстве пользователя (vendor/lib64/libdiag.so) содержит
 * вариант B. Лоадер пробует оба варианта и адаптируется автоматически.
 */

#include <cstdint>
#include <string>

typedef unsigned char byte;
typedef unsigned char boolean;
typedef unsigned char uint8;
typedef uint16_t diag_dci_peripherals;

// ── Общие константы ──────────────────────────────────────────────────────────
#define DIAG_DCI_NO_ERROR   1001
#define DIAG_DCI_ERROR      0
#define DCI_DISABLE         0
#define DCI_ENABLE          1

// proc type для локального модема (MSM = 0)
#define DIAG_PROC_MSM       0
// data_type для diag_register_dci_client (вариант B)
#define DIAG_DATA_TYPE_DCI  1

// ── Тип DCI stream callback (одинаков в обоих вариантах) ─────────────────────
extern "C" {
typedef void (*diag_dci_stream_cb_t)(unsigned char* buf, int len, void* context);
}

// ── Сигнатуры Вариант A ───────────────────────────────────────────────────────
typedef boolean (*fn_Diag_LSM_Init)         (byte* pIEnv);
typedef boolean (*fn_Diag_LSM_DeInit)       (void);
typedef int     (*fn_diag_allocate_client)  (void);
typedef int     (*fn_diag_release_client)   (int client_id);

// ── Сигнатуры Вариант B (CAF / vendor libdiag) ────────────────────────────────
//
// int diag_register_dci_client(int *client_id,
//                              diag_dci_req_table_t *tbl,  // NULL = нет доп. команд
//                              int   count,                 // 0
//                              int   data_type)             // DIAG_DATA_TYPE_DCI = 1
//
// int diag_release_dci_client(int *client_id)
//
typedef int (*fn_diag_register_dci_client)  (int* client_id,
                                             diag_dci_peripherals*,
                                             int   channel,
                                             void*   signal_type);
typedef int (*fn_diag_release_dci_client)   (int* client_id);

// ── Общие функции (присутствуют в обоих вариантах) ───────────────────────────
typedef int (*fn_diag_register_dci_stream_proc)(
        int                  client_id,
        diag_dci_stream_cb_t log_cb,
        diag_dci_stream_cb_t event_cb
);

typedef int (*fn_diag_log_stream_config)(
        int       client_id,
        int       enable,
        uint16_t* log_codes,
        int       num_codes
);

typedef int (*fn_diag_event_stream_config)(
        int       client_id,
        int       enable,
        uint32_t* event_ids,
        int       num_ids
);

// Опциональные
typedef int (*fn_diag_send_dci_async_req)(
        int            client_id,
        unsigned char* buf,
        int            bytes,
        unsigned char* rsp_ptr,
        int            rsp_len,
        void         (*rsp_cb)(unsigned char*, int, void*),
        void*          data_ptr
);
typedef int (*fn_diag_get_health_stats_proc)(int client_id, void* stats, int proc);

/* This API provides information about the peripherals that support
   DCI in a given processor. Input Parameters are:
   a) processor id
   b) pointer to a diag_dci_peripherals variable to store the bit mask */
typedef int (*fn_diag_get_dci_support_list_proc)(int proc, diag_dci_peripherals *list);

// ─────────────────────────────────────────────────────────────────────────────
class LibdiagLoader {
public:
    enum class ApiVariant {
        Unknown,
        VariantA,   // diag_allocate_client / diag_release_client
        VariantB,   // diag_register_dci_client / diag_release_dci_client
    };

    static LibdiagLoader& instance();

    // Загрузить и резолвить символы. true = все обязательные найдены.
    bool load();
    void unload();

    // Set custom path to libdiag.so (call before load())
    // Used from JNI to point to app's nativeLibraryDir copy
    static void set_custom_path(const char* path);

    bool is_loaded()  const { return handle_ != nullptr || variant_ != ApiVariant::Unknown; }
    ApiVariant variant() const { return variant_; }
    const std::string& loaded_path() const { return loaded_path_; }

    // ── LSM init/deinit (общие) ───────────────────────────────────────────────
    fn_Diag_LSM_Init   lsm_init   = nullptr;
    fn_Diag_LSM_DeInit lsm_deinit = nullptr;

    // ── Управление клиентом — Вариант A ──────────────────────────────────────
    fn_diag_allocate_client  allocate_client  = nullptr;
    fn_diag_release_client   release_client   = nullptr;

    // ── Управление клиентом — Вариант B ──────────────────────────────────────
    fn_diag_register_dci_client register_dci_client = nullptr;
    fn_diag_release_dci_client  release_dci_client  = nullptr;

    // ── Stream (общие) ────────────────────────────────────────────────────────
    fn_diag_register_dci_stream_proc register_dci_stream  = nullptr;
    fn_diag_log_stream_config        log_stream_config    = nullptr;
    fn_diag_event_stream_config      event_stream_config  = nullptr;

    // ── Опциональные ──────────────────────────────────────────────────────────
    fn_diag_send_dci_async_req   send_dci_async_req = nullptr;
    fn_diag_get_health_stats_proc get_health_stats  = nullptr;

    fn_diag_get_dci_support_list_proc diag_get_dci_support_list_proc = nullptr;

private:
    LibdiagLoader() = default;
    ~LibdiagLoader();
    LibdiagLoader(const LibdiagLoader&)            = delete;
    LibdiagLoader& operator=(const LibdiagLoader&) = delete;

    bool try_load(const char* path);
    void* resolve(const char* sym, bool mandatory = true);

    void*       handle_      = nullptr;
    std::string loaded_path_;
    ApiVariant  variant_     = ApiVariant::Unknown;
};

#endif // LIBDIAG_LOADER_H