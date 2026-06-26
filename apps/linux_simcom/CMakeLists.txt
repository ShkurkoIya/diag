cmake_minimum_required(VERSION 3.22)
project(diag_scan CXX)

find_package(Threads REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# Таргет: Исполняемый файл Linux-демона
# ─────────────────────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME})

# Явно привязываем исходники по твоей структуре слоев (core, io, protocol)
target_sources(diag_scan_exe PRIVATE
    main.cpp

    core/ce_thread.cpp
    core/ce_serial.cpp

    io/serial_port.cpp
    io/serial_port_stream.cpp

    protocol/diag_request.cpp
    protocol/hdlc_serializer.cpp
    protocol/hdlc_stream_parser.cpp
)

# Настраиваем инклуды.
# Для исполняемого файла они ВСЕГДА PRIVATE, так как от него никто не наследуется.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
)

# Задаем стандарты и флаги сборки локально для приложения
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2)

# Прокидываем дефайн режима TTY локально
target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_TTY_MODE=1)

# Линкуем зависимости.
# diag_parsers_core передаст сюда ВСЕ свои PUBLIC инклуды (включая protocol/)
# и автоматически прокинет макросы вроде FEATURE_GSM или USE_ASN1C_LTE.
target_link_libraries(${PROJECT_NAME} PRIVATE
    diag_parsers_core
    Threads::Threads
)

# Задаем свойства бинарника: чистый исполняемый файл Linux без префиксов и .so
set_target_properties(diag_scan_exe PROPERTIES
    OUTPUT_NAME "diag_scan_exe"
    PREFIX ""
    SUFFIX ""
)
