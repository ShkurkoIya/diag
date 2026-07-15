cmake_minimum_required(VERSION 3.22)
project(observer_core LANGUAGES CXX)

# Запрещаем GCC ругаться на варнинги в коде
add_compile_options(-Wno-error -Wno-stringop-overflow)

# 1. Жестко привязываем наш локальный sysroot, скопированный из srsRAN
set(SRSRAN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/srsran_sysroot/include")

# Точные пути к добытым изолированным библиотекам парсинга
set(SRSRAN_ASN1_LIB    "${CMAKE_SOURCE_DIR}/srsran_sysroot/lib/librrc_asn1.a")
set(SRSRAN_NR_ASN1_LIB "${CMAKE_SOURCE_DIR}/srsran_sysroot/lib/librrc_nr_asn1.a")
set(SRSRAN_UTILS_LIB   "${CMAKE_SOURCE_DIR}/srsran_sysroot/lib/libasn1_utils.a")

message(STATUS "─────────────────────────────────────────────────────────────")
message(STATUS "[srsRAN] Изолированные промышленные парсеры 3GPP подключены:")
message(STATUS "  Headers: ${SRSRAN_INCLUDE_DIR}")
message(STATUS "  LTE Lib: ${SRSRAN_ASN1_LIB}")
message(STATUS "  5G  Lib: ${SRSRAN_NR_ASN1_LIB}")
message(STATUS "─────────────────────────────────────────────────────────────")

# 2. ТАРГЕТ: Статическая библиотека ядра радиосканера
add_library(observer STATIC)

target_sources(observer PRIVATE
    journal.cpp
    qualcomm_log_parser.cpp
)

target_compile_options(observer PRIVATE -Wall -Wextra -O3)
target_compile_features(observer PRIVATE cxx_std_17)

# НАСТРОЙКА ИНКЛЮДОВ ЯДРА
target_include_directories(observer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol
        ${SRSRAN_INCLUDE_DIR}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# ЛИНКОВКА: Привязываем чистые ASN1 библиотеки напрямую файлами!
target_link_libraries(observer
    PUBLIC
        ${SRSRAN_ASN1_LIB}
        ${SRSRAN_NR_ASN1_LIB}
        ${SRSRAN_UTILS_LIB}
    PRIVATE
        m dl
)

add_library(observer::core ALIAS observer)

# Локальные ручные модули автора (2G/3G)
include(cmake/RegisterRatModule.cmake)
register_rat_module("gsm"   CONDITION USE_GSM   DEFINE FEATURE_GSM   SOURCES gsm/diag_gsm_log_parser.cpp)
register_rat_module("wcdma" CONDITION USE_WCDMA DEFINE FEATURE_WCDMA SOURCES wcdma/diag_wcdma_log_parser.cpp)
register_rat_module("umts"  CONDITION USE_UMTS  DEFINE FEATURE_UMTS  SOURCES umts/diag_umts_log_parser.cpp umts/asn1c_umts_bridge.cpp)
register_rat_module("lte"   CONDITION USE_LTE   DEFINE FEATURE_LTE   SOURCES lte/diag_lte_rrc_parser.cpp lte/diag_lte_nas_parser.cpp lte/diag_lte_ml1_parser.cpp lte/asn1c_lte_bridge.cpp)
register_rat_module("nr"    CONDITION USE_NR    DEFINE FEATURE_NR    SOURCES nr/diag_nr_log_parser.cpp)
