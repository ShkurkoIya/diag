cmake_minimum_required(VERSION 3.22)
project(observer C CXX)

# Подключаем функцию-генератор автогена кода утилитой asn1c
include(cmake/Asn1cGenerator.cmake)
include(cmake/RegisterRatModule.cmake)
include(cmake/RegisterAsn1GenLib.cmake)

# ─────────────────────────────────────────────────────────────────────────────
# 1. ИЗОЛИРОВАННЫЕ ОПЦИИ (Если не заданы в корне монорепо, выставляем дефолты)
# ─────────────────────────────────────────────────────────────────────────────
if(NOT DEFINED BUILD_FULL_RAT)
  option(BUILD_FULL_RAT "Enable all Radio Access Technologies (GSM, WCDMA, UMTS, LTE, 5G NR)" ON)
endif()

set(RAT_DEFAULT OFF)
if(BUILD_FULL_RAT)
  set(RAT_DEFAULT ON)
endif()

if(NOT DEFINED USE_GSM)  option(USE_GSM "Build with GSM (2G) parser module" ON)endif()
if(NOT DEFINED USE_WCDMA)  option(USE_WCDMA "Build with WCDMA (3G) parser module" ${RAT_DEFAULT})endif()
if(NOT DEFINED USE_UMTS)  option(USE_UMTS "Build with UMTS (3G RRC) parser module" ${RAT_DEFAULT})endif()
if(NOT DEFINED USE_LTE)  option(USE_LTE "Build with LTE (4G) parser module" ON)endif()
if(NOT DEFINED USE_NR)  option(USE_NR "Build with NR (5G) parser module" ${RAT_DEFAULT})endif()

if(NOT DEFINED USE_ASN1C_LTE)  option(USE_ASN1C_LTE "Build with asn1c-generated LTE RRC decoder" ${RAT_DEFAULT})endif()
if(NOT DEFINED USE_ASN1C_UMTS)  option(USE_ASN1C_UMTS "Build with asn1c-generated UMTS/WCDMA RRC decoder" ${RAT_DEFAULT})endif()

# ─────────────────────────────────────────────────────────────────────────────
# 3. ТАРГЕТ: Создаем статическую библиотеку фреймворка observer
# ─────────────────────────────────────────────────────────────────────────────
add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    journal.cpp
    qualcomm_log_parser.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17 c_std_11)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Виртуальный экспорт родительской папки 'libs' под префиксом <observer/...>
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# Интерфейсный псевдоним SDK для внешних приложений
add_library(observer::core ALIAS ${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} PUBLIC m dl)

# ─────────────────────────────────────────────────────────────────────────────
# 4. ДЕКЛАРАТИВНОЕ ПОДКЛЮЧЕНИЕ ВСЕХ RAT МОДУЛЕЙ
# ─────────────────────────────────────────────────────────────────────────────

register_rat_module("gsm"   CONDITION USE_GSM   DEFINE FEATURE_GSM   SOURCES gsm/diag_gsm_log_parser.cpp)
register_rat_module("wcdma" CONDITION USE_WCDMA DEFINE FEATURE_WCDMA SOURCES wcdma/diag_wcdma_log_parser.cpp)
register_rat_module("nr"    CONDITION USE_NR    DEFINE FEATURE_NR    SOURCES nr/diag_nr_log_parser.cpp)

register_rat_module("umts"
    CONDITION USE_UMTS
    DEFINE    FEATURE_UMTS
    SOURCES
        umts/diag_umts_log_parser.cpp
        umts/asn1c_umts_bridge.cpp
)

register_rat_module("lte"
    CONDITION USE_LTE
    DEFINE    FEATURE_LTE
    SOURCES
        lte/diag_lte_rrc_parser.cpp
        lte/diag_lte_nas_parser.cpp
        lte/diag_lte_ml1_parser.cpp
        lte/lte_rrc_ota_decoder.cpp
        lte/lte_sib1_decoder.cpp
        lte/asn1c_lte_bridge.cpp
)

# ─────────────────────────────────────────────────────────────────────────────
# 5. ДЕКЛАРАТИВНАЯ СБОРКА ТЯЖЕЛОГО АВТОГЕНА (Идеальная чистота)
# ─────────────────────────────────────────────────────────────────────────────

# Полный цикл генерации и упаковки сошек для UMTS
register_asn1_decoder("umts"
    CONDITION USE_UMTS AND USE_ASN1C_UMTS
    DEFINE    USE_ASN1C_UMTS
    ASN_FILE  asn1/UMTS-RRC-Definitions.asn
    SOLIB_FILE umts/umts_rrc_solib.cpp
)

# Полный цикл генерации и упаковки сошек для LTE
register_asn1_decoder("lte"
    CONDITION USE_LTE AND USE_ASN1C_LTE
    DEFINE    USE_ASN1C_LTE
    ASN_FILE  asn1/EUTRA-RRC-Definitions.asn
    SOLIB_FILE lte/lte_rrc_solib.cpp
)

# Полный цикл генерации и упаковки сошек для NR
register_asn1_decoder("nr"
    CONDITION USE_NR AND USE_ASN1C_NR
    DEFINE    USE_ASN1C_NR
    ASN_FILE  asn1/NR-RRC-Definitions.asn
    SOLIB_FILE nr/nr_rrc_solib.cpp
)
