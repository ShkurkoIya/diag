// ═════════════════════════════════════════════════════════════════════════════
// Cell Lock / EFS2 additions to diag_jni.cpp
//
// Append this BELOW the existing nativeGetLibdiagPath() function.
// Also add #include "diag_cell_lock.h" at the top of diag_jni.cpp.
// ═════════════════════════════════════════════════════════════════════════════

// Add this global next to g_client / g_snapshot:
// static DiagCellLock* g_locker = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// nativeLockLte(earfcn: Int, pci: Int): Boolean
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jboolean, nativeLockLte)(JNIEnv* /*env*/, jobject /*thiz*/,
                                     jint earfcn, jint pci) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_client || g_client->state() != DiagDciClient::State::Running) {
        LOGE("nativeLockLte: client not running");
        return JNI_FALSE;
    }
    if (!g_locker) {
        g_locker = new DiagCellLock(g_client->client_id());
    }
    int ret = g_locker->lock_lte(static_cast<uint32_t>(earfcn),
                                  static_cast<uint16_t>(pci));
    if (ret == 0) {
        LOGI("LTE cell locked: EARFCN=%d PCI=%d", earfcn, pci);
    } else {
        LOGE("LTE cell lock failed: errno=%d", g_locker->efs().last_errno());
    }
    return ret == 0 ? JNI_TRUE : JNI_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// nativeUnlockLte(): Boolean
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jboolean, nativeUnlockLte)(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_locker) return JNI_TRUE;  // nothing to unlock
    int ret = g_locker->unlock_lte();
    LOGI("LTE cell unlocked: ret=%d", ret);
    return ret == 0 ? JNI_TRUE : JNI_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// nativeLockNr(nrarfcn: Int, pci: Int, scs: Int): Boolean
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jboolean, nativeLockNr)(JNIEnv* /*env*/, jobject /*thiz*/,
                                    jint nrarfcn, jint pci, jint scs) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_client || g_client->state() != DiagDciClient::State::Running) {
        return JNI_FALSE;
    }
    if (!g_locker) {
        g_locker = new DiagCellLock(g_client->client_id());
    }
    int ret = g_locker->lock_nr_earfcn(static_cast<uint32_t>(nrarfcn),
                                        static_cast<uint8_t>(scs));
    if (ret == 0 && pci >= 0) {
        ret = g_locker->lock_nr_pci(static_cast<uint32_t>(nrarfcn),
                                     static_cast<uint16_t>(pci),
                                     static_cast<uint8_t>(scs));
    }
    LOGI("NR cell lock: ARFCN=%d PCI=%d SCS=%d ret=%d", nrarfcn, pci, scs, ret);
    return ret == 0 ? JNI_TRUE : JNI_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// nativeUnlockNr(): Boolean
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jboolean, nativeUnlockNr)(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_locker) return JNI_TRUE;
    int ret = g_locker->unlock_nr();
    LOGI("NR cell unlocked: ret=%d", ret);
    return ret == 0 ? JNI_TRUE : JNI_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// nativeIsLocked(): Boolean
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jboolean, nativeIsLocked)(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_locker) return JNI_FALSE;
    return g_locker->is_lte_locked() ? JNI_TRUE : JNI_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// nativeEfsListDir(path: String): Array<String>
// Returns array of "TYPE|MODE|SIZE|NAME" strings
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jobjectArray, nativeEfsListDir)(JNIEnv* env, jobject /*thiz*/,
                                            jstring jpath) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_client || g_client->state() != DiagDciClient::State::Running) {
        return nullptr;
    }
    if (!g_locker) {
        g_locker = new DiagCellLock(g_client->client_id());
    }

    const char* path = env->GetStringUTFChars(jpath, nullptr);
    std::vector<EfsEntry> entries;
    g_locker->efs().listdir(path, entries);
    env->ReleaseStringUTFChars(jpath, path);

    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(entries.size()), strCls, nullptr);

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "%s|%s|%u|%s",
                 e.type_str().c_str(), e.mode_str().c_str(), e.size, e.name.c_str());
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), to_jstring(env, buf));
    }
    return arr;
}

// ─────────────────────────────────────────────────────────────────────────────
// nativeEfsReadFile(path: String): ByteArray?
// ─────────────────────────────────────────────────────────────────────────────
extern "C"
JNI_METHOD(jbyteArray, nativeEfsReadFile)(JNIEnv* env, jobject /*thiz*/,
                                           jstring jpath) {
    std::lock_guard<std::mutex> lk(g_init_mx);
    if (!g_client || !g_locker) return nullptr;

    const char* path = env->GetStringUTFChars(jpath, nullptr);
    std::vector<uint8_t> data;
    int ret = g_locker->efs().read_file(path, data);
    if (ret < 0) ret = g_locker->efs().get_item(path, data);
    env->ReleaseStringUTFChars(jpath, path);

    if (data.empty()) return nullptr;

    jbyteArray arr = env->NewByteArray(static_cast<jsize>(data.size()));
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(data.size()),
                            reinterpret_cast<const jbyte*>(data.data()));
    return arr;
}
