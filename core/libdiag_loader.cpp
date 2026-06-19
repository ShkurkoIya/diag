#include "libdiag_loader.h"
#include "diag_common.h"
#include <dlfcn.h>
#include <cstring>

// Порядок поиска: сначала app-local пути (доступны из app namespace),
// потом стандартные системные/вендорские пути (работают из root console)
static const char* kLibdiagPaths[] = {
        // App-local: копия libdiag.so в директории приложения
        // (Kotlin должен скопировать туда перед System.loadLibrary)
        "libdiag.so",                              // LD_LIBRARY_PATH / same dir as caller
        "/data/local/tmp/libdiag.so",              // доступно для app namespace
        // Стандартные пути (работают из su -c binary или root)
        "/system/lib64/libdiag.so",
        "/system/lib/libdiag.so",
        "/vendor/lib64/libdiag.so",
        "/vendor/lib/libdiag.so",
        "/system/vendor/lib64/libdiag.so",
        "/system/vendor/lib/libdiag.so",
        nullptr
};

// Custom path set from JNI (before load())
static const char* g_custom_libdiag_path = nullptr;

void LibdiagLoader::set_custom_path(const char* path) {
    g_custom_libdiag_path = path;
}

// ─────────────────────────────────────────────────────────────────────────────
LibdiagLoader& LibdiagLoader::instance() {
    static LibdiagLoader inst;
    return inst;
}

LibdiagLoader::~LibdiagLoader() { unload(); }

// ─────────────────────────────────────────────────────────────────────────────
bool LibdiagLoader::load() {
    if (handle_) return true;

    // Try custom path first (set from JNI with app's nativeLibraryDir)
    if (g_custom_libdiag_path) {
        if (try_load(g_custom_libdiag_path)) {
            DIAG_LOGI("libdiag: loaded from custom path %s (variant %s)",
                      loaded_path_.c_str(),
                      variant_ == ApiVariant::VariantA ? "A" :
                      variant_ == ApiVariant::VariantB ? "B" : "?");
            return true;
        }
    }

    for (const char** p = kLibdiagPaths; *p; ++p) {
        if (try_load(*p)) {
            DIAG_LOGI("libdiag: loaded from %s (variant %s)",
                      loaded_path_.c_str(),
                      variant_ == ApiVariant::VariantA ? "A (allocate_client)" :
                      variant_ == ApiVariant::VariantB ? "B (register_dci_client)" : "?");
            return true;
        }
    }

    DIAG_LOGE("libdiag: could not find libdiag.so in any known path");
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
bool LibdiagLoader::try_load(const char* path) {
    void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        DIAG_LOGD("libdiag: dlopen(%s) failed: %s", path, dlerror());
        return false;
    }

    handle_      = h;
    loaded_path_ = path;
    variant_     = ApiVariant::Unknown;

    // ── Обязательные символы, общие для обоих вариантов ─────────────────────
    lsm_init   = (fn_Diag_LSM_Init)   resolve("Diag_LSM_Init");
    lsm_deinit = (fn_Diag_LSM_DeInit) resolve("Diag_LSM_DeInit");

    // ── Определяем вариант API ───────────────────────────────────────────────
    // Вариант A: diag_allocate_client
//    void* alloc_a = resolve("diag_allocate_client", /*mandatory=*/false);
//    void* rel_a   = resolve("diag_release_client",  /*mandatory=*/false);

    // Вариант B: diag_register_dci_client
    void* alloc_b = resolve("diag_register_dci_client", false);
    void* rel_b   = resolve("diag_release_dci_client",  false);

//    if (alloc_a && rel_a) {
//        variant_        = ApiVariant::VariantA;
//        allocate_client = (fn_diag_allocate_client) alloc_a;
//        release_client  = (fn_diag_release_client)  rel_a;
//        DIAG_LOGD("libdiag: using Variant A (allocate_client)");
//    } else if (alloc_b && rel_b) {
//        variant_            = ApiVariant::VariantB;
//        register_dci_client = (fn_diag_register_dci_client) alloc_b;
//        release_dci_client  = (fn_diag_release_dci_client)  rel_b;
//        DIAG_LOGD("libdiag: using Variant B (register_dci_client)");
//    } else {
//        // Нет ни того ни другого — не наша библиотека
//        DIAG_LOGE("libdiag: missing client management symbols in %s "
//                  "(no allocate_client AND no register_dci_client)", path);
//        unload();
//        return false;
//    }
    if (alloc_b && rel_b) {
        variant_            = ApiVariant::VariantB;
        register_dci_client = (fn_diag_register_dci_client) alloc_b;
        release_dci_client  = (fn_diag_release_dci_client)  rel_b;
        DIAG_LOGD("libdiag: using Variant B (register_dci_client)");
    } else {
        // Нет ни того ни другого — не наша библиотека
        DIAG_LOGE("libdiag: missing client management symbols in %s "
                  "(no allocate_client AND no register_dci_client)", path);
        unload();
        return false;
    }
    // ── Обязательные stream-символы ─────────────────────────────────────────
    register_dci_stream = (fn_diag_register_dci_stream_proc)
            resolve("diag_register_dci_stream_proc");
    log_stream_config   = (fn_diag_log_stream_config)
            resolve("diag_log_stream_config");
    event_stream_config = (fn_diag_event_stream_config)
            resolve("diag_event_stream_config");

    diag_get_dci_support_list_proc = (fn_diag_get_dci_support_list_proc) resolve("diag_get_dci_support_list_proc");

    bool ok = lsm_init            != nullptr
              && lsm_deinit          != nullptr
              && register_dci_stream != nullptr
              && log_stream_config   != nullptr
              && event_stream_config != nullptr
              && diag_get_dci_support_list_proc != nullptr;
    if (!ok) {
        DIAG_LOGE("libdiag: missing mandatory stream symbols in %s", path);
        unload();
        return false;
    }

    // ── Опциональные ────────────────────────────────────────────────────────
    send_dci_async_req = (fn_diag_send_dci_async_req)
            resolve("diag_send_dci_async_req", false);
    get_health_stats   = (fn_diag_get_health_stats_proc)
            resolve("diag_get_health_stats_proc", false);


    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void* LibdiagLoader::resolve(const char* sym, bool mandatory) {
    if (!handle_) return nullptr;
    dlerror();
    void* ptr = dlsym(handle_, sym);
    const char* err = dlerror();
    if (err) {
        if (mandatory) DIAG_LOGE("libdiag: dlsym(%s) failed: %s", sym, err);
        else           DIAG_LOGD("libdiag: optional dlsym(%s) not found", sym);
        return nullptr;
    }
    DIAG_LOGD("libdiag: resolved %s @ %p", sym, ptr);
    return ptr;
}

// ─────────────────────────────────────────────────────────────────────────────
void LibdiagLoader::unload() {
    lsm_init            = nullptr;
    lsm_deinit          = nullptr;
    allocate_client     = nullptr;
    release_client      = nullptr;
    register_dci_client = nullptr;
    release_dci_client  = nullptr;
    register_dci_stream = nullptr;
    log_stream_config   = nullptr;
    event_stream_config = nullptr;
    send_dci_async_req  = nullptr;
    get_health_stats    = nullptr;
    variant_            = ApiVariant::Unknown;

    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
        loaded_path_.clear();
        DIAG_LOGI("libdiag: unloaded");
    }
}