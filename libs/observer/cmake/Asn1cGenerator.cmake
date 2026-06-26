function(add_asn1_library TARGET_NAME ASN_FILE OUTPUT_DIR)
  # Ищем asn1c в системе
  find_program(ASN1C_EXECUTABLE asn1c REQUIRED)

  # Маркерный файл для контроля сборки CMake
  set(MARKER_FILE "${OUTPUT_DIR}/pdu_collection.c")

  # КРОСС-ПЛАТФОРМЕННЫЙ ФИКС ТИПОВ (Учитываем Android, Linux и Windows)
  set(ASN1C_COMPAT_FLAGS "")
  if(NOT ANDROID)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      set(ASN1C_COMPAT_FLAGS "-include stddef.h -include sys/types.h -include signal.h")
    elseif(MSVC)
      set(ASN1C_COMPAT_FLAGS "/FIstddef.h /FIsys/types.h /FIsignal.h")
    endif()
  endif()

  # Кастомная команда для генерации исходников на лету
  add_custom_command(
        OUTPUT ${MARKER_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
        COMMAND ${ASN1C_EXECUTABLE} -pdu=all -fcompound-names -gen-PER ${ASN_FILE}
        COMMAND ${CMAKE_COMMAND} -E remove -f ${OUTPUT_DIR}/converter-example.c
        WORKING_DIRECTORY ${OUTPUT_DIR}
        DEPENDS ${ASN_FILE}
        COMMENT "Генерация ASN.1 декодера [${TARGET_NAME}] из ${ASN_FILE}..."
    )

  # Ленивый поиск сгенерированных .c файлов
  file(GLOB_RECURSE GENERATED_SOURCES CONFIGURE_DEPENDS "${OUTPUT_DIR}/*.c")
  if(NOT GENERATED_SOURCES)
    set(GENERATED_SOURCES ${MARKER_FILE})
  endif()

  # Создаем статическую библиотеку автогена
  add_library(${TARGET_NAME} STATIC ${GENERATED_SOURCES})

  # Прокидываем инклуды и специфичные для компилятора флаги
  target_include_directories(${TARGET_NAME} PUBLIC ${OUTPUT_DIR})

  # Глушим варнинги автогена (-w) и добавляем наши адаптивные флаги
  target_compile_options(${TARGET_NAME} PRIVATE -w ${ASN1C_COMPAT_FLAGS})

  target_compile_definitions(${TARGET_NAME} PUBLIC ASN_PDU_COLLECTION=1)
  set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

endfunction()
