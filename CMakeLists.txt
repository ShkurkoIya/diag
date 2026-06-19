cmake_minimum_required(VERSION 3.22)
project(diag_scanner C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ─────────────────────────────────────────────────────────────────────────────
# Include search paths — every subdir, so plain "#include "x.h"" still works
# ─────────────────────────────────────────────────────────────────────────────
set(COMMON_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/parsers
        ${CMAKE_CURRENT_SOURCE_DIR}/parsers/lte
        ${CMAKE_CURRENT_SOURCE_DIR}/parsers/gsm
        ${CMAKE_CURRENT_SOURCE_DIR}/parsers/wcdma
        ${CMAKE_CURRENT_SOURCE_DIR}/parsers/umts
        ${CMAKE_CURRENT_SOURCE_DIR}/parsers/nr
        ${CMAKE_CURRENT_SOURCE_DIR}/modem
        ${CMAKE_CURRENT_SOURCE_DIR}/dci
        ${CMAKE_CURRENT_SOURCE_DIR}/jni
)

set(COMMON_COMPILE_OPTS -Wall -Wextra -Wno-unused-parameter -O2)

find_library(log-lib log)
set(COMMON_LIBS dl m)
if(log-lib)
        list(APPEND COMMON_LIBS ${log-lib})
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Option: build the asn1c-generated LTE RRC decoder
# ─────────────────────────────────────────────────────────────────────────────
option(USE_ASN1C_LTE "Build with asn1c-generated LTE RRC decoder" OFF)
option(USE_ASN1C_UMTS "Build with asn1c-generated UMTS/WCDMA RRC decoder" OFF)

if(USE_ASN1C_LTE)
        set(ASN1C_LTE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/parsers/lte/asn1c_lte")

        if(NOT EXISTS "${ASN1C_LTE_DIR}/asn_application.h")
                message(FATAL_ERROR
                        "${ASN1C_LTE_DIR}/asn_application.h missing.\n"
                        "Did you copy <reference>/Parser/LTElib/build/* here?")
        endif()
        if(NOT EXISTS "${ASN1C_LTE_DIR}/pdu_collection.c")
                message(FATAL_ERROR
                        "${ASN1C_LTE_DIR}/pdu_collection.c missing.\n"
                        "asn1c was invoked WITHOUT -pdu=all flag.")
        endif()

        file(GLOB ASN1_LTE_SOURCES "${ASN1C_LTE_DIR}/*.c")
        list(FILTER ASN1_LTE_SOURCES EXCLUDE REGEX ".*converter-example\\.c$")

        list(LENGTH ASN1_LTE_SOURCES ASN1_LTE_FILE_COUNT)
        message(STATUS "asn1c LTE: ${ASN1_LTE_FILE_COUNT} sources from ${ASN1C_LTE_DIR}")

        if(ASN1_LTE_FILE_COUNT LESS 50)
                message(FATAL_ERROR "Only ${ASN1_LTE_FILE_COUNT} sources — too few.")
        endif()

        add_library(rrc_asn1_lte STATIC ${ASN1_LTE_SOURCES})
        target_include_directories(rrc_asn1_lte PUBLIC "${ASN1C_LTE_DIR}")
        target_compile_options(rrc_asn1_lte PRIVATE -w)
        target_compile_definitions(rrc_asn1_lte PUBLIC ASN_PDU_COLLECTION=1)
        set_target_properties(rrc_asn1_lte PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(USE_ASN1C_UMTS)
        set(ASN1C_UMTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/parsers/umts/asn1c_umts")

        if(NOT EXISTS "${ASN1C_UMTS_DIR}/asn_application.h")
                message(FATAL_ERROR
                        "${ASN1C_UMTS_DIR}/asn_application.h missing.\n"
                        "Did you copy <reference>/Parser/UMTSlib/build/* here?")
        endif()
        if(NOT EXISTS "${ASN1C_UMTS_DIR}/pdu_collection.c")
                message(FATAL_ERROR
                        "${ASN1C_UMTS_DIR}/pdu_collection.c missing.\n"
                        "asn1c was invoked WITHOUT -pdu=all flag.")
        endif()

        file(GLOB ASN1_UMTS_SOURCES "${ASN1C_UMTS_DIR}/*.c")
        list(FILTER ASN1_UMTS_SOURCES EXCLUDE REGEX ".*converter-example\\.c$")

        list(LENGTH ASN1_UMTS_SOURCES ASN1_UMTS_FILE_COUNT)
        message(STATUS "asn1c UMTS: ${ASN1_UMTS_FILE_COUNT} sources from ${ASN1C_UMTS_DIR}")

        if(ASN1_UMTS_FILE_COUNT LESS 50)
                message(FATAL_ERROR "Only ${ASN1_UMTS_FILE_COUNT} UMTS sources — too few.")
        endif()

        add_library(rrc_asn1_umts STATIC ${ASN1_UMTS_SOURCES})
        target_include_directories(rrc_asn1_umts PUBLIC "${ASN1C_UMTS_DIR}")
        target_compile_options(rrc_asn1_umts PRIVATE -w)
        target_compile_definitions(rrc_asn1_umts PUBLIC ASN_PDU_COLLECTION=1)
        set_target_properties(rrc_asn1_umts PROPERTIES POSITION_INDEPENDENT_CODE ON)

        # ── Isolated UMTS RRC decoder shared object ──────────────────────────────
        add_library(diag_umts_rrc SHARED parsers/umts/umts_rrc_solib.cpp)
        target_link_libraries(diag_umts_rrc PRIVATE rrc_asn1_umts)
        target_include_directories(diag_umts_rrc PRIVATE "${ASN1C_UMTS_DIR}")
        target_compile_options(diag_umts_rrc PRIVATE -w -fvisibility=hidden)
        target_link_options(diag_umts_rrc PRIVATE -Wl,--exclude-libs,ALL)
        set_target_properties(diag_umts_rrc PROPERTIES
                OUTPUT_NAME "diag_umts_rrc" PREFIX "lib")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Parser sources — organized by RAT
# ─────────────────────────────────────────────────────────────────────────────

# ── core ──
set(CORE_SOURCES
        core/journal.cpp
)

# ── per-RAT ──
set(GSM_SOURCES parsers/gsm/diag_gsm_log_parser.cpp)
set(WCDMA_SOURCES parsers/wcdma/diag_wcdma_log_parser.cpp
        parsers/umts/asn1c_umts_bridge.cpp)
set(UMTS_SOURCES parsers/umts/diag_umts_log_parser.cpp)
set(NR_SOURCES parsers/nr/diag_nr_log_parser.cpp)

# ── LTE: 3-way split + decoders ──
set(LTE_SOURCES
        parsers/lte/diag_lte_rrc_parser.cpp
        parsers/lte/diag_lte_nas_parser.cpp
        parsers/lte/diag_lte_ml1_parser.cpp
        parsers/lte/lte_rrc_ota_decoder.cpp
        parsers/lte/lte_sib1_decoder.cpp
        parsers/lte/asn1c_lte_bridge.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/parsers/lte/lte_sib1_decoder_asn1c.cpp")
        list(APPEND LTE_SOURCES parsers/lte/lte_sib1_decoder_asn1c.cpp)
endif()

# ── top-level dispatcher ──
set(DISPATCHER_SOURCES
        parsers/qualcomm_log_parser.cpp
)

# ── modem control (cell-lock, EFS browse) ──
set(MODEM_SOURCES
        # (intentionally empty — header-only logic)
)

# Базовый набор парсеров, общий для обеих платформ
set(ALL_PARSER_SOURCES
        ${CORE_SOURCES}
        ${GSM_SOURCES}
        ${WCDMA_SOURCES}
        ${UMTS_SOURCES}
        ${NR_SOURCES}
        ${LTE_SOURCES}
        ${DISPATCHER_SOURCES}
        ${MODEM_SOURCES}
)

# ─────────────────────────────────────────────────────────────────────────────
# НОВАЯ ОПЦИЯ: Сборка под Linux (SIMCom / TTY) вместо Android (DCI / JNI)
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_FOR_LINUX "Build natively for Linux with SIMCom TTY serial port" OFF)

if(BUILD_FOR_LINUX)
        message(STATUS "[Target OS] Configuring for Native Linux (SIMCom TTY)...")

        # Добавляем папку с файлами Linux-порта в инклюды
        list(APPEND COMMON_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/core/linux_serial)

        # Исходники модуля последовательного порта Linux
        set(LINUX_SERIAL_SOURCES
                core/serial_port.cpp
                core/ce_serial.cpp
                core/diag_request.cpp
                core/ce_thread.cpp
                core/hdlc_serializer.cpp
                core/hdlc_stream_parser.cpp
                core/crc16_generic.h
        )

        # Собираем чистый исполняемый файл Linux
        add_executable(diag_scan_exe
                main_linux.cpp
                ${ALL_PARSER_SOURCES}
                ${LINUX_SERIAL_SOURCES}
        )

        target_include_directories(diag_scan_exe PRIVATE ${COMMON_INCLUDES})
        target_compile_options(diag_scan_exe PRIVATE ${COMMON_COMPILE_OPTS})

        # Прокидываем дефайн в код для разделения main() через #ifdef
        target_compile_definitions(diag_scan_exe PRIVATE LINUX_TTY_MODE=1)

        # Линкуем только системные библиотеки Linux (pthread обязателен для CeThread)
        target_link_libraries(diag_scan_exe PRIVATE dl m pthread)

        if(USE_ASN1C_LTE)
                target_compile_definitions(diag_scan_exe PRIVATE USE_ASN1C_LTE=1)
                target_link_libraries(diag_scan_exe PRIVATE rrc_asn1_lte)
        endif()

        if(USE_ASN1C_UMTS)
                add_dependencies(diag_scan_exe diag_umts_rrc)
        endif()

        # Убираем все префиксы и расширения .so — делаем стандартный Linux-бинарник
        set_target_properties(diag_scan_exe PROPERTIES
                OUTPUT_NAME "diag_scan_exe"
                PREFIX ""
                SUFFIX ""
        )

else()
        # ─────────────────────────────────────────────────────────────────────────
        # ОРИГИНАЛЬНАЯ СБОРКА ПОД ANDROID (100% нетронутая логика)
        # ─────────────────────────────────────────────────────────────────────────
        message(STATUS "[Target OS] Configuring for Android (Original)...")

        # Возвращаем Android-специфичные файлы лоадера и DCI в сборку
        set(ANDROID_SPECIFIC_SOURCES
                core/libdiag_loader.cpp
                dci/diag_dci_client.cpp
        )

        add_executable(diag_scan_exe
                main.cpp
                ${ALL_PARSER_SOURCES}
                ${ANDROID_SPECIFIC_SOURCES}
        )

        target_include_directories(diag_scan_exe PRIVATE ${COMMON_INCLUDES})
        target_compile_options(diag_scan_exe PRIVATE ${COMMON_COMPILE_OPTS})

        if(USE_ASN1C_LTE)
                target_compile_definitions(diag_scan_exe PRIVATE USE_ASN1C_LTE=1)
                target_link_libraries(diag_scan_exe PRIVATE rrc_asn1_lte)
        endif()

        if(USE_ASN1C_UMTS)
                add_dependencies(diag_scan_exe diag_umts_rrc)
        endif()

        target_link_libraries(diag_scan_exe PRIVATE ${COMMON_LIBS} ${CMAKE_DL_LIBS})

        # Оригинальные свойства сборки бинарника-маскировки под .so для Android
        set_target_properties(diag_scan_exe PROPERTIES
                OUTPUT_NAME "diag_scan_exe"
                PREFIX "lib"
                SUFFIX ".so"
                POSITION_INDEPENDENT_CODE ON
        )
        target_link_options(diag_scan_exe PRIVATE -pie)

        # Опциональная сборка JNI библиотеки (только для Android-режима)
        option(BUILD_JNI_LIB "Also build libdiag_jni.so for direct Kotlin JNI calls" OFF)
        if(BUILD_JNI_LIB)
                add_library(diag_jni SHARED
                        jni/diag_jni.cpp
                        jni/diag_jni_lock_additions.cpp
                )
                target_include_directories(diag_jni PRIVATE ${COMMON_INCLUDES})
                target_compile_options(diag_jni PRIVATE ${COMMON_COMPILE_OPTS})
                target_link_libraries(diag_jni PRIVATE ${COMMON_LIBS})
        endif()
endif()
