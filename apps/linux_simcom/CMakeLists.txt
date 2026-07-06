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

# ПОЧИНЕНО ПО BEST PRACTICES: Исключаем кашу. Оставляем только корень приложения.
target_include_directories(diag_scan_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(diag_scan_exe PRIVATE cxx_std_17)
target_compile_options(diag_scan_exe PRIVATE -Wall -Wextra -O2 -pthread)
target_compile_definitions(diag_scan_exe PRIVATE LINUX_TTY_MODE=1)

target_link_libraries(diag_scan_exe PRIVATE
    observer::core
    Threads::Threads
)

# Явная защита от гонок сборки тяжелого автогена
if(TARGET asn1c_host_compiler)
  add_dependencies(diag_scan_exe asn1c_host_compiler)
endif()

set_target_properties(diag_scan_exe PROPERTIES
    OUTPUT_NAME "diag_scan_exe"
    PREFIX ""
    SUFFIX ""
)
