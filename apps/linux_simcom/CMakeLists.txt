cmake_minimum_required(VERSION 3.22)
find_package(Threads REQUIRED)

add_executable(diag_scan_exe)

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

# Только внутренние директории самого демона!
target_include_directories(diag_scan_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
)

target_compile_features(diag_scan_exe PRIVATE cxx_std_17)
target_compile_options(diag_scan_exe PRIVATE -Wall -Wextra -O2)
target_compile_definitions(diag_scan_exe PRIVATE LINUX_TTY_MODE=1)

# Магия Modern CMake: линкуем observer::core, и бинарник САМ забирает все пути инклудов ядра!
target_link_libraries(diag_scan_exe PRIVATE
    observer::core
    Threads::Threads
)

set_target_properties(diag_scan_exe PROPERTIES
    OUTPUT_NAME "diag_scan_exe"
    PREFIX ""
    SUFFIX ""
)
