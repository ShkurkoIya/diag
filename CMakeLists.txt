cmake_minimum_required(VERSION 3.22)
project(observer_monorepo C CXX)

# Глобальные опции платформ
option(BUILD_FOR_LINUX   "Build natively for Linux (SIMCom TTY Daemon)" ON)
option(BUILD_FOR_ANDROID "Build for Android NDK (DCI/JNI Library)" OFF)

# Мета-опция: Сборка всех сетевых модулей разом (Modern CMake Preset)
option(BUILD_FULL_RAT "Enable all Radio Access Technologies (GSM, WCDMA, UMTS, LTE, 5G NR)" ON)

# Глобальные флаги подавления варнингов компилятора GCC 14+ для бесперебойной сборки srsRAN
add_compile_options(-Wno-error -Wno-stringop-overflow -Wno-unused-parameter -Wno-unused-variable)

# ─────────────────────────────────────────────────────────────────────────────
# 3. ЛОКАЛЬНЫЕ ОПЦИИ СЕТЕЙ (Каскадная логика с абсолютным приоритетом)
# ─────────────────────────────────────────────────────────────────────────────
if(BUILD_FULL_RAT)
  # FORCE насильно перезаписывает значения в кэше CMake.
  set(USE_GSM   ON  CACHE BOOL "Build with GSM (2G) parser module" FORCE)
  set(USE_WCDMA ON  CACHE BOOL "Build with WCDMA (3G) parser module" FORCE)
  set(USE_UMTS  ON  CACHE BOOL "Build with UMTS (3G RRC) parser module" FORCE)
  set(USE_LTE   ON  CACHE BOOL "Build with LTE (4G) parser module" FORCE)
  set(USE_NR    ON  CACHE BOOL "Build with NR (5G) parser module" FORCE)

  set(USE_ASN1C_LTE  ON CACHE BOOL "Build with asn1c-generated LTE RRC decoder" FORCE)
  set(USE_ASN1C_UMTS ON CACHE BOOL "Build with asn1c-generated UMTS/WCDMA RRC decoder" FORCE)
else()
  # Если FULL_RAT выключен, то включаются обычные мягкие опции
  option(USE_GSM   "Build with GSM (2G) parser module" OFF)
  option(USE_WCDMA "Build with WCDMA (3G) parser module" OFF)
  option(USE_UMTS  "Build with UMTS (3G RRC) parser module" OFF)
  option(USE_LTE   "Build with LTE (4G) parser module" ON)
  option(USE_NR    "Build with NR (5G) parser module" OFF)

  option(USE_ASN1C_LTE  "Build with asn1c-generated LTE RRC decoder" OFF)
  option(USE_ASN1C_UMTS "Build with asn1c-generated UMTS/WCDMA RRC decoder" OFF)
endif()

option(USE_GSM   "Build with GSM (2G) parser module" ${USE_GSM_DEFAULT})
option(USE_WCDMA "Build with WCDMA (3G) parser module" ${USE_WCDMA_DEFAULT})
option(USE_UMTS  "Build with UMTS (3G RRC) parser module" ${USE_UMTS_DEFAULT})
option(USE_LTE  "Build with LTE (4G) parser module" ${USE_LTE_DEFAULT})
option(USE_NR    "Build with NR (5G) parser module" ${USE_NR_DEFAULT})

option(USE_ASN1C_LTE  "Build with asn1c-generated LTE RRC decoder" ${USE_ASN1C_LTE_DEFAULT})
option(USE_ASN1C_UMTS "Build with asn1c-generated UMTS/WCDMA RRC decoder" ${USE_ASN1C_UMTS_DEFAULT})

# Саммари конфигурации в консоль Zed
message(STATUS "─────────────────────────────────────────────────────────────")
message(STATUS "=== Observer Monorepo Configuration Summary ===")
message(STATUS "  Targets:")
message(STATUS "    Linux Simcom App:     ${BUILD_FOR_LINUX}")
message(STATUS "    Android NDK App:      ${BUILD_FOR_ANDROID}")
message(STATUS "  Meta Options:")
message(STATUS "    BUILD_FULL_RAT:       ${BUILD_FULL_RAT}")
message(STATUS "  Parser Modules (RAT):")
message(STATUS "    GSM (2G):             ${USE_GSM}")
message(STATUS "    WCDMA (3G):           ${USE_WCDMA}")
message(STATUS "    UMTS (3G RRC):        ${USE_UMTS} (ASN1C: ${USE_ASN1C_UMTS})")
message(STATUS "    LTE (4G):             ${USE_LTE} (ASN1C: ${USE_ASN1C_LTE})")
message(STATUS "    NR (5G):              ${USE_NR}")
message(STATUS "─────────────────────────────────────────────────────────────")

# Подключаем ядро фреймворка Observer
add_subdirectory(libs/observer)

if(BUILD_FOR_LINUX)
  add_subdirectory(apps/linux_simcom)
endif()

if(BUILD_FOR_ANDROID)
  add_subdirectory(apps/android)
endif()
