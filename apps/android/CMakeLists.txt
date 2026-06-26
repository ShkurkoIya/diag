cmake_minimum_required(VERSION 3.22)
project(diag_scan_android CXX)

# Ищем системную библиотеку логирования Android NDK по канонам Modern CMake
find_library(ANDROID_LOG_LIBRARY log REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# Таргет 1: Основной исполняемый файл Android (Маскирующийся под .so для Gradle)
# ─────────────────────────────────────────────────────────────────────────────
add_executable(diag_scan_exe)

target_sources(diag_scan_exe PRIVATE
    main.cpp
    libdiag_loader.cpp
    dci/diag_dci_client.cpp
)

target_include_directories(diag_scan_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/dci
)

target_compile_features(diag_scan_exe PRIVATE cxx_std_17)
target_compile_options(diag_scan_exe PRIVATE -Wall -Wextra -O2)

# Линкуем ядро Observer (оно само прокинет инклуды) и логгер NDK
target_link_libraries(diag_scan_exe PRIVATE
    observer::core
    ${ANDROID_LOG_LIBRARY}
)

# Реализуем оригинальные требования Android Gradle паковщика:
# Префикс "lib", расширение ".so", POSITION_INDEPENDENT_CODE и флаг линкера -pie
set_target_properties(diag_scan_exe PROPERTIES
    OUTPUT_NAME "diag_scan_exe"
    PREFIX      "lib"
    SUFFIX      ".so"
    POSITION_INDEPENDENT_CODE ON
)
target_link_options(diag_scan_exe PRIVATE -pie)

# ─────────────────────────────────────────────────────────────────────────────
# Таргет 2: JNI Библиотека для прямых вызовов из Kotlin/Java Activity
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_JNI_LIB "Also build libdiag_jni.so for direct Kotlin JNI calls" OFF)

if(BUILD_JNI_LIB)
  message(STATUS "[Android] Configuring shared JNI library target...")

  add_library(diag_jni SHARED)

  target_sources(diag_jni PRIVATE
        jni/diag_jni.cpp
        jni/diag_jni_lock_additions.cpp
    )

  target_include_directories(diag_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/jni
    )

  target_compile_features(diag_jni PRIVATE cxx_std_17)
  target_compile_options(diag_jni PRIVATE -Wall -Wextra -O2)

  target_link_libraries(diag_jni PRIVATE
        observer::core
        ${ANDROID_LOG_LIBRARY}
    )

  set_target_properties(diag_jni PROPERTIES
        OUTPUT_NAME "diag_jni"
        PREFIX      "lib"
        SUFFIX      ".so"
    )
endif()
