cmake_minimum_required(VERSION 3.22)

# УБРАЛИ project(diag_scan_linux CXX), так как компиляторы уже подняты в корне!

# Ищем системные потоки (POSIX Threads)
find_package(Threads REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# Таргет: Исполняемый файл Linux-демона
# ─────────────────────────────────────────────────────────────────────────────
add_executable(diag_scan_exe)

# Привязываем исходники приложения согласно структуре слоев (core, io, protocol)
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

# Настраиваем локальные инклуды приложения
target_include_directories(diag_scan_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
)

# Стандарты и флаги компиляции
target_compile_features(diag_scan_exe PRIVATE cxx_std_17)
target_compile_options(diag_scan_exe PRIVATE -Wall -Wextra -O2)

# Локальный дефайн режима работы через TTY
target_compile_definitions(diag_scan_exe PRIVATE LINUX_TTY_MODE=1)

# Линкуем зависимости
target_link_libraries(diag_scan_exe PRIVATE
    observer::core   # Автоматически прокинет инклуды парсеров и дефайны
    Threads::Threads
)

# Задаем свойства бинарника: чистый исполняемый файл Linux без префиксов
set_target_properties(diag_scan_exe PROPERTIES
    OUTPUT_NAME "diag_scan_exe"
    PREFIX ""
    SUFFIX ""
)
