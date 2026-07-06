include(ExternalProject)
include_guard(GLOBAL)

message(STATUS "[ASN.1] Настройка 3GPP-компилятора asn1c (mouse07410)...")

find_program(SYSTEM_ASN1C_EXE asn1c)
if(SYSTEM_ASN1C_EXE)
  execute_process(
        COMMAND ${SYSTEM_ASN1C_EXE} -h
        OUTPUT_VARIABLE ASN1C_HELP
        ERROR_VARIABLE ASN1C_HELP_ERR
    )
  if(ASN1C_HELP MATCHES "gen-PER")
    set(ASN1C_EXECUTABLE "${SYSTEM_ASN1C_EXE}" CACHE INTERNAL "Path to 3GPP asn1c")
    add_custom_target(asn1c_host_compiler)
    message(STATUS "[ASN.1] Найден подходящий системный компилятор: ${ASN1C_EXECUTABLE}")
    return()
  endif()
endif()

set(ASN1C_PREFIX "${CMAKE_BINARY_DIR}/external/asn1c")
set(ASN1C_EXECUTABLE "${ASN1C_PREFIX}/bin/asn1c" CACHE INTERNAL "Path to custom 3GPP asn1c")

if(NOT TARGET asn1c_host_compiler)
  if(NOT CMAKE_HOST_C_COMPILER)
    set(CMAKE_HOST_C_COMPILER "cc")
  endif()

  # ПОЧИНЕНО: Используем встроенные токены <SOURCE_DIR>, чтобы CMake железно
  # знал, где лежит сгенерированный скрипт configure, даже если сборка идет в изолированной подпапке.
  ExternalProject_Add(
        asn1c_host_compiler
        GIT_REPOSITORY     https://github.com/mouse07410/asn1c
        GIT_TAG            vlm_master
        PREFIX             "${CMAKE_BINARY_DIR}/external/asn1c_build"

        # Запуск генератора Autotools строго внутри директории с исходниками
        PATCH_COMMAND      autoreconf -ivf

        # ПОЧИНЕНО: Жесткий абсолютный путь к скрипту конфигурации через токен <SOURCE_DIR>
        CONFIGURE_COMMAND  <SOURCE_DIR>/configure --prefix=${ASN1C_PREFIX} CC=${CMAKE_HOST_C_COMPILER}

        BUILD_COMMAND      $(MAKE) -j
        INSTALL_COMMAND    $(MAKE) install
        BUILD_BYPRODUCTS   "${ASN1C_EXECUTABLE}"
        LOG_DOWNLOAD       ON
        LOG_CONFIGURE      ON
        LOG_BUILD          ON
    )
endif()
