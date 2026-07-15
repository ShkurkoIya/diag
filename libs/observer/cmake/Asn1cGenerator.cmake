include_guard(GLOBAL)

#[[
    Функция add_asn1_library
    Генерирует исходники с помощью утилиты asn1c строго на этапе СБОРКИ.
    Идеально работает в связке с ExternalProject_Add без гонок за файлы.
]]
function(add_asn1_library TARGET_NAME ASN_FILE OUTPUT_DIR)
  get_filename_component(ABSOLUTE_ASN_FILE "${ASN_FILE}" ABSOLUTE)

  # 1. Создаем целевую папку на этапе конфигурации
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")

  # 2. Объявляем маркер завершения генерации
  set(TIMESTAMP_FILE "${OUTPUT_DIR}/asn1c_run.timestamp")

  # 3. Регистрируем команду генерации 3GPP-кода на этапе СБОРКИ
  add_custom_command(
      OUTPUT "${TIMESTAMP_FILE}"
      COMMAND "${CMAKE_BINARY_DIR}/external/asn1c/bin/asn1c"
              -pdu=all
              -fcompound-names
              -fprefer-import-source
              -gen-UPER
              -gen-APER
              -D "${OUTPUT_DIR}"
              "${ABSOLUTE_ASN_FILE}"
      # Очистка мусора средствами CMake
      COMMAND ${CMAKE_COMMAND} -E remove -f "${OUTPUT_DIR}/converter-example.c"
      COMMAND ${CMAKE_COMMAND} -E remove -f "${OUTPUT_DIR}/pdu_collection.c"
      COMMAND ${CMAKE_COMMAND} -E touch "${TIMESTAMP_FILE}"
      DEPENDS "${ABSOLUTE_ASN_FILE}"
      COMMENT "[ASN.1] Запуск компилятора соты для таргета: ${TARGET_NAME}"
  )

  # 4. Создаем кастомный таргет-генератор, который зависит от маркера
  set(GEN_TARGET "${TARGET_NAME}_codegen")
  add_custom_target(${GEN_TARGET} DEPENDS "${TIMESTAMP_FILE}")

  # Жестко привязываем генератор к хост-компилятору из ExternalProject
  if(TARGET asn1c_host_compiler)
    add_dependencies(${GEN_TARGET} asn1c_host_compiler)
  endif()

  # 5. Создаем ОБЪЕКТНУЮ библиотеку.
  # Так как список файлов неизвестен при конфигурации, мы пишем пустой триггер,
  # а инклуды подхватятся компилятором, когда файлы будут сгенерированы.
  file(WRITE "${OUTPUT_DIR}/empty_trigger.c" "/* Автогенерация ASN.1 */\n")

  add_library(${TARGET_NAME} OBJECT "${OUTPUT_DIR}/empty_trigger.c")

  # Связываем таргет библиотеки с таргетом генератора кода
  add_dependencies(${TARGET_NAME} ${GEN_TARGET})

  # Настройки компиляции
  set_target_properties(${TARGET_NAME} PROPERTIES
      LINKER_LANGUAGE C
      POSITION_INDEPENDENT_CODE ON
      UNITY_BUILD OFF
  )

  # Оформляем интерфейсы инклудов и компиляции
  target_include_directories(${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${OUTPUT_DIR}>)
  target_compile_options(${TARGET_NAME} PRIVATE -w)
  target_compile_definitions(${TARGET_NAME} PUBLIC ASN_PDU_COLLECTION=1)
endfunction()
