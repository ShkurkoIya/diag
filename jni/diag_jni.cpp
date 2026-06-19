/*
 * diag_jni.cpp — JNI bridge for DiagScanner
 *
 * Kotlin class: stc.forensic.observer.diag.DiagManager
 *
 * Methods:
 *   nativeSetLibdiagPath(path)           — set dlopen path BEFORE init
 *   nativeInit(): Boolean                — start DCI
 *   nativeDestroy()                      — stop DCI
 *   nativeLockCell(mode, ver, freq, id)  — lock cell
 *   nativeUnlockCell(): Int              — unlock
 *   nativeRebootModem(): Int             — reboot modem
 *   nativeIsRunning(): Boolean
 *   nativeGetLastError(): String
 */

#include "diag_dci_client.h"
#include "diag_cell_lock.h"
#include "diag_common.h"

#include <jni.h>
#include <android/log.h>
#include <mutex>
#include <string>

#define TAG "DiagJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ─── JNI naming ──────────────────────────────────────────────────────────────
#define JNI_PKG    stc_forensic_observer_diag
#define JNI_CLS    DiagManager
#define JNI_METHOD(ret, name) \
    JNIEXPORT ret JNICALL Java_ ## JNI_PKG ## _ ## JNI_CLS ## _ ## name

// ─── Globals ─────────────────────────────────────────────────────────────────
static DiagDciClient* g_client = nullptr;
static DiagCellLock*  g_locker = nullptr;
static std::mutex     g_mx;
static std::string    g_last_error;
static std::string    g_libdiag_path;

static void set_error(const std::string& msg) {
    g_last_error = msg;
    LOGE("%s", msg.c_str());
}

static jstring to_jstr(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

// nativeSetLibdiagPath(path: String)
extern "C"
JNIEXPORT void JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeSetLibdiagPath(
        JNIEnv* env,
        jobject /* thiz */,
        jstring jpath) {
    const char* p = env->GetStringUTFChars(jpath, nullptr);
    g_libdiag_path = p;
    env->ReleaseStringUTFChars(jpath, p);
    LibdiagLoader::set_custom_path(g_libdiag_path.c_str());
    LOGI("libdiag path: %s", g_libdiag_path.c_str());
}

// nativeInit(): Boolean
extern "C"
JNIEXPORT jboolean JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeInit(
        JNIEnv* /* env */,
        jobject /* thiz */) {
    std::lock_guard<std::mutex> lk(g_mx);

    if (g_client && g_client->state() == DiagDciClient::State::Running)
        return JNI_TRUE;

    delete g_locker; g_locker = nullptr;
    delete g_client; g_client = nullptr;

    g_client = new DiagDciClient();
    if (!g_client->start()) {
        set_error("DCI start failed");
        delete g_client; g_client = nullptr;
        return JNI_FALSE;
    }

    g_locker = new DiagCellLock(g_client->client_id());
    g_locker->set_debug(true);
    LOGI("DCI started, client_id=%d", g_client->client_id());
    return JNI_TRUE;
}

// nativeDestroy()
extern "C"
JNIEXPORT void JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeDestroy(
        JNIEnv* /* env */,
        jobject /* thiz */) {
    std::lock_guard<std::mutex> lk(g_mx);
    delete g_locker; g_locker = nullptr;
    if (g_client) { g_client->stop(); delete g_client; g_client = nullptr; }
    LOGI("Destroyed");
}

// nativeLockCell(mode: String, version: String, earfcn: Int, pci: Int): Int
extern "C"
JNIEXPORT jint JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeLockCell(
        JNIEnv* env,
        jobject /* thiz */,
        jstring jmode,
        jstring jversion,
        jint earfcn,
        jint pci) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_locker) { set_error("Not initialized"); return -1; }

    const char* mc = env->GetStringUTFChars(jmode, nullptr);
    const char* vc = env->GetStringUTFChars(jversion, nullptr);
    std::string mode(mc), version(vc);
    env->ReleaseStringUTFChars(jmode, mc);
    env->ReleaseStringUTFChars(jversion, vc);

    int ret = -1;

    if (mode == "4g") {
        if (version == "v1" || version.empty())
            ret = g_locker->lock_lte_v1((uint16_t)earfcn, (uint16_t)pci);
        else if (version == "v2")
            ret = g_locker->lock_lte_v2((uint32_t)earfcn, (uint16_t)pci);
        else if (version == "v3") {
            BtsLteLockList list{};
            list.len = 1;
            list.cell[0].freq = (uint32_t)earfcn;
            list.cell[0].pci  = (uint16_t)pci;
            ret = g_locker->lock_lte_v3(list);
        }
    } else if (mode == "3g") {
        if (version == "v1" || version.empty())
            ret = g_locker->lock_umts_by_freq((uint16_t)earfcn);
        else if (version == "v2")
            ret = g_locker->lock_umts((uint16_t)pci, (uint32_t)earfcn);
    } else if (mode == "2g") {
        ret = g_locker->lock_gsm((uint16_t)earfcn);
    } else {
        set_error("Unknown mode: " + mode);
        return -2;
    }

    if (ret == 0)
        LOGI("Lock OK: %s/%s %d:%d", mode.c_str(), version.c_str(), earfcn, pci);
    else
        set_error("Lock failed: errno=" + std::to_string(g_locker->last_errno()));
    return ret;
}

// nativeUnlockCell(): Int
extern "C"
JNIEXPORT jint JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeUnlockCell(
        JNIEnv* /* env */,
        jobject /* thiz */) {

    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_locker) return -1;
    return g_locker->unlock_lte();
}

// nativeRebootModem(): Int
extern "C"
JNIEXPORT jint JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeRebootModem(
        JNIEnv* /* env */,
        jobject /* thiz */) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_client) { set_error("Not initialized"); return -1; }
    g_client->rebootModem();
    LOGI("Modem reboot sent");
    return 0;
}

// nativeIsRunning(): Boolean
extern "C"
JNIEXPORT jboolean JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeIsRunning(
        JNIEnv* /* env */,
        jobject /* thiz */) {
    std::lock_guard<std::mutex> lk(g_mx);
    return (g_client && g_client->state() == DiagDciClient::State::Running)
           ? JNI_TRUE : JNI_FALSE;
}

// nativeGetLastError(): String
extern "C"
JNIEXPORT jstring JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeGetLastError(
        JNIEnv* env,
        jobject /* thiz */) {
    return to_jstr(env, g_last_error);
}

// nativeGetLibdiagInfo(): String
extern "C"
JNIEXPORT jstring JNICALL
Java_stc_forensic_observer_diag_DiagManager_nativeGetLibdiagInfo(
        JNIEnv* env,
        jobject /* thiz */) {
    const auto& p = LibdiagLoader::instance().loaded_path();
    return to_jstr(env, p.empty() ? "(not loaded)" : p);
}
