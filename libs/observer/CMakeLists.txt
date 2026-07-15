cmake_minimum_required(VERSION 3.22)

# Подключаем наши декларативные макросы
include(cmake/Asn1cCompilerSetup.cmake)
include(cmake/Asn1cGenerator.cmake)
include(cmake/RegisterRatModule.cmake)
include(cmake/RegisterAsn1GenLib.cmake)

# 1. БЕЗОПАСНЫЕ ОПЦИИ ИЗОЛЯЦИИ (Включаются ТОЛЬКО если папка libs/observer собирается отдельно)
get_property(IS_SUBPROJECT GLOBAL PROPERTY IN_SUBPROJECT)
if(NOT IS_SUBPROJECT)
  project(observer_standalone LANGUAGES C CXX)
  option(BUILD_FULL_RAT "Enable all Radio Access Technologies" ON)
  option(USE_GSM        "Build with GSM"                      ON)
  option(USE_WCDMA      "Build with WCDMA"                    ON)
  option(USE_UMTS       "Build with UMTS"                     ON)
  option(USE_LTE        "Build with LTE"                      ON)
  option(USE_NR         "Build with NR 5G"                    ON)
  option(USE_ASN1C_LTE  "Use asn1c for LTE"                   ON)
  option(USE_ASN1C_UMTS "Use asn1c for UMTS"                  ON)
endif()

if(TARGET observer_asn1_lte)
  add_dependencies(observer observer_asn1_lte)
endif()

if(TARGET observer_asn1_umts)
  add_dependencies(observer observer_asn1_umts)
endif()

# 2. ТАРГЕТ: Статическая библиотека ядра парсеров
add_library(observer STATIC)

target_sources(observer PRIVATE
    journal.cpp
    qualcomm_log_parser.cpp
)

# Компиляторные флаги/generated/asn1c_umts
target_compile_options(observer PRIVATE -Wall -Wextra -O2)

# НАСТРОЙКА ИНКЛЮДОВ ПО BEST PRACTICES
target_include_directories(observer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol
        ${CMAKE_CURRENT_BINARY_DIR}/generated/asn1c_lte
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/asn1c_umts
    PUBLIC
        # Позволяет писать #include "libs/observer/protocol/diagcmd.h" в приложениях
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(observer PUBLIC m dl)
add_library(observer::core ALIAS observer)

# 3. ДЕКЛАРАТИВНОЕ ПОДКЛЮЧЕНИЕ МОДУЛЕЙ ТЕХНОЛОГИЙ (RAT)
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
        lte/lte_sib1_decoder_asn1c.cpp
        lte/asn1c_lte_bridge.cpp
)

# 4. ДЕКЛАРАТИВНАЯ СБОРКА ТЯЖЕЛОГО АВТОГЕНА (ASN.1)
register_asn1_decoder("umts"
    CONDITION USE_UMTS AND USE_ASN1C_UMTS
    DEFINE    USE_ASN1C_UMTS
    ASN_FILE  asn1/UMTS-RRC-Definitions.asn
    SOLIB_FILE umts/umts_rrc_solib.cpp
)

register_asn1_decoder("lte"
    CONDITION USE_LTE AND USE_ASN1C_LTE
    DEFINE    USE_ASN1C_LTE
    ASN_FILE  asn1/EUTRA-RRC-Definitions.asn
    SOLIB_FILE lte/lte_rrc_ota_decoder.cpp
)

if(TARGET observer_asn1_lte)
  add_dependencies(observer observer_asn1_lte)
  target_link_libraries(observer PRIVATE observer_asn1_lte)
endif()

# if(TARGET observer_asn1_umts)
#   add_dependencies(observer observer_asn1_umts)
#   target_link_libraries(observer PRIVATE observer_asn1_umts)
# endif()

if(TARGET asn1c_host_compiler)
  add_dependencies(observer asn1c_host_compiler)
endif()
