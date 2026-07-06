cmake_minimum_required(VERSION 3.22)

find_library(ANDROID_LOG_LIBRARY log REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# Таргет 1: Исполняемый файл (Маскирующийся под .so для прохода через Gradle)
# ─────────────────────────────────────────────────────────────────────────────
set(EXE_TARGET "diag_scan_exe")
add_executable(${EXE_TARGET})

target_sources(${EXE_TARGET} PRIVATE
    main.cpp
    libdiag_loader.cpp
    dci/diag_dci_client.cpp
)

# ПОЧИНЕНО: Оставляем только чистый корень инклудов модуля
target_include_directories(${EXE_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(${EXE_TARGET} PRIVATE cxx_std_17)
target_compile_options(${EXE_TARGET} PRIVATE -Wall -Wextra -O2)

target_link_libraries(${EXE_TARGET} PRIVATE
    observer::core
    ${ANDROID_LOG_LIBRARY}
)

if(TARGET asn1c_host_compiler)
  add_dependencies(${EXE_TARGET} asn1c_host_compiler)
endif()

set_target_properties(${EXE_TARGET} PROPERTIES
    OUTPUT_NAME "diag_scan_exe"
    PREFIX      "lib"
    SUFFIX      ".so"
)
# Жестко прокидываем PIE флаги для Android 5.0+
target_link_options(${EXE_TARGET} PRIVATE -pie -fPIE)

# ─────────────────────────────────────────────────────────────────────────────
# Таргет 2: Честная JNI Shared-библиотека для вызовов из Kotlin Activity
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_JNI_LIB "Also build libdiag_jni.so for direct Kotlin JNI calls" OFF)

if(BUILD_JNI_LIB)
  set(JNI_TARGET "diag_jni")
  add_library(${JNI_TARGET} SHARED)

  target_sources(${JNI_TARGET} PRIVATE
        jni/diag_jni.cpp
        jni/diag_jni_lock_additions.cpp
    )

  target_include_directories(${JNI_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

  target_compile_features(${JNI_TARGET} PRIVATE cxx_std_17)
  target_compile_options(${JNI_TARGET} PRIVATE -Wall -Wextra -O2)

  target_link_libraries(${JNI_TARGET} PRIVATE
        observer::core
        ${ANDROID_LOG_LIBRARY}
    )

  if(TARGET asn1c_host_compiler)
    add_dependencies(${JNI_TARGET} asn1c_host_compiler)
  endif()

  set_target_properties(${JNI_TARGET} PROPERTIES
        OUTPUT_NAME "diag_jni"
        PREFIX      "lib"
        SUFFIX      ".so"
  )
endif()
