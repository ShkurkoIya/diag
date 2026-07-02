cmake_minimum_required(VERSION 3.22)

# Никаких повторных project()! Используем контекст главного монорепозитория.

# Подключаем наши обновленные, безопасные макросы генерации и регистрации
include(cmake/Asn1cCompilerSetup.cmake)
include(cmake/Asn1cGenerator.cmake)
include(cmake/RegisterRatModule.cmake)
include(cmake/RegisterAsn1GenLib.cmake)

# ─────────────────────────────────────────────────────────────────────────────
# 1. ИЗОЛИРОВАННЫЕ ОПЦИИ (Для запуска папки libs/observer в standalone-режиме)
# ─────────────────────────────────────────────────────────────────────────────
if(NOT DEFINED BUILD_FULL_RAT)
  option(BUILD_FULL_RAT "Enable all Radio Access Technologies" ON)
endif()

# Если переменные не пришли из корня монорепозитория, выставляем девелоперские дефолты
if(NOT DEFINED USE_GSM)
  option(USE_GSM "Build with GSM" ON)
endif()

if(NOT DEFINED USE_WCDMA)
  option(USE_WCDMA "Build with WCDMA" ON)
endif()

if(NOT DEFINED USE_UMTS)
  option(USE_UMTS "Build with UMTS" ON)
endif()

if(NOT DEFINED USE_LTE)
  option(USE_LTE "Build with LTE" ON)
endif()

if(NOT DEFINED USE_NR)
  option(USE_NR "Build with NR 5G" ON)
endif()

if(NOT DEFINED USE_ASN1C_LTE)
  option(USE_ASN1C_LTE "Use asn1c for LTE" ON)
endif()

if(NOT DEFINED USE_ASN1C_UMTS)
  option(USE_ASN1C_UMTS "Use asn1c for UMTS" ON)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# 2. ТАРГЕТ: Статическая библиотека ядра парсеров
# ─────────────────────────────────────────────────────────────────────────────
add_library(observer STATIC)

# Базовые диспетчеры и логирование, общие для всех технологий
target_sources(observer PRIVATE
    journal.cpp
    qualcomm_log_parser.cpp
)

# Ограничиваем область видимости флагов компиляции (только для сборки ядра)
target_compile_features(observer PRIVATE cxx_std_17 c_std_11)
target_compile_options(observer PRIVATE -Wall -Wextra -O2)

# Настройка путей заголовочных файлов
target_include_directories(observer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
        # Позволяет писать #include <observer/protocol/diagcmd.h> в приложениях
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# Системные зависимости ядра
target_link_libraries(observer PUBLIC m dl)

# Экспортируем каноничный алиас для линковки внешними приложениями
add_library(observer::core ALIAS observer)

# ─────────────────────────────────────────────────────────────────────────────
# 3. ДЕКЛАРАТИВНОЕ ПОДКЛЮЧЕНИЕ МОДУЛЕЙ ТЕХНОЛОГИЙ (RAT)
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
        # Подключаем оба декодера, разделение логики будет внутри .cpp через дефайны
        lte/lte_sib1_decoder.cpp
        lte/lte_sib1_decoder_asn1c.cpp
        lte/asn1c_lte_bridge.cpp
)

# ─────────────────────────────────────────────────────────────────────────────
# 4. ДЕКЛАРАТИВНАЯ СБОРКА ТЯЖЕЛОГО АВТОГЕНА
# ─────────────────────────────────────────────────────────────────────────────

# Полный цикл генерации и упаковки декодера для UMTS
# register_asn1_decoder("umts"
#     CONDITION USE_UMTS AND USE_ASN1C_UMTS
#     DEFINE    USE_ASN1C_UMTS
#     ASN_FILE  asn1/UMTS-RRC-Definitions.asn
#     SOLIB_FILE umts/umts_rrc_solib.cpp
# )

# Полный цикл генерации и упаковки декодера для LTE
# Передаем основной файл спецификации. Скрипт сам сделает пути абсолютными.
register_asn1_decoder("lte"
    CONDITION USE_LTE AND USE_ASN1C_LTE
    DEFINE    USE_ASN1C_LTE
    ASN_FILE  asn1/EUTRA-RRC-Definitions.asn
    SOLIB_FILE umts/umts_rrc_solib.cpp # Если для LTE нужен свой solib-враппер, укажи его файл здесь!
)

# ВАЖНО: Модуль для "nr" убран, так как физически файл asn1/NR-RRC-Definitions.asn отсутствует.
