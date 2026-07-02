include(FetchContent)

message(STATUS "[ASN.1] Разворачиваем правильный 3GPP-компилятор asn1c (mouse07410)...")

# Скачиваем форк, который умеет парсить расширения 3GPP [[ ]] и PER-кодирование
FetchContent_Declare(
    asn1c_compiler
    GIT_REPOSITORY https://github.com/mouse07410/asn1c.git
    GIT_TAG        vlm_master # Стабильная ветка для телеком-стеков
)

FetchContent_GetProperties(asn1c_compiler)
if(NOT asn1c_compiler_POPULATED)
  FetchContent_Populate(asn1c_compiler)

  # Собираем бинарник прямо во время конфигурации, чтобы он был доступен сразу
  set(ASN1C_SRC_DIR "${asn1c_compiler_SOURCE_DIR}")
  set(ASN1C_BIN_DIR "${asn1c_compiler_BINARY_DIR}/bin")

  # Нам нужен только исполняемый файл для сборки под хост-систему
  find_program(MAKE_EXE NAMES gmake make REQURED)

  # Запускаем автосборку компилятора (один раз на чистый билд)
  if(NOT EXISTS "${ASN1C_BIN_DIR}/asn1c")
    message(STATUS "[ASN.1] Компиляция бинарника asn1c на хосте (может занять полминуты)...")
    execute_process(
            COMMAND ./configure --prefix=${asn1c_compiler_BINARY_DIR}
            WORKING_DIRECTORY "${ASN1C_SRC_DIR}"
            RESULT_VARIABLE CONF_RES
        )
    execute_process(
            COMMAND ${MAKE_EXE} -j
            WORKING_DIRECTORY "${ASN1C_SRC_DIR}"
            RESULT_VARIABLE MAKE_RES
        )
    execute_process(
            COMMAND ${MAKE_EXE} install
            WORKING_DIRECTORY "${ASN1C_SRC_DIR}"
            RESULT_VARIABLE INSTALL_RES
        )
  endif()

  # Экспортируем путь к нашему локальному, всеядному компилятору
  set(ASN1C_EXECUTABLE "${asn1c_compiler_BINARY_DIR}/bin/asn1c" CACHE INTERNAL "Path to custom 3GPP asn1c")
endif()

message(STATUS "[ASN.1] Локальный компилятор готов: ${ASN1C_EXECUTABLE}")
