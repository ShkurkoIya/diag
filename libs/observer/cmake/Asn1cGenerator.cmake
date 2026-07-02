include_guard(GLOBAL)

#[[
    Функция add_asn1_library
    Генерирует исходники с помощью утилиты asn1c и упаковывает их в изолированный таргет.
    Использует концепт Unity Build для обхода ограничений файлового сканирования CMake.
]]
function(add_asn1_library TARGET_NAME ASN_FILE OUTPUT_DIR)
  # 1. Жесткий поиск утилиты компилятора ASN.1
  find_program(ASN1C_EXECUTABLE asn1c REQUIRED)

  # Убеждаемся, что путь к ASN файлу абсолютный
  get_filename_component(ABSOLUTE_ASN_FILE "${ASN_FILE}" ABSOLUTE)

  # Главный маркерный файл-враппер для Unity сборки
  set(UNITY_SOURCE_FILE "${OUTPUT_DIR}/asn1_unity_build.c")

  # 2. Адаптивные кросс-платформенные флаги типов
  set(ASN1C_COMPAT_FLAGS "")
  if(NOT ANDROID)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      list(APPEND ASN1C_COMPAT_FLAGS "-include" "stddef.h" "-include" "sys/types.h" "-include" "signal.h")
    elseif(MSVC)
      list(APPEND ASN1C_COMPAT_FLAGS "/FIstddef.h" "/FIsys/types.h" "/FIsignal.h")
    endif()
  endif()

  # 3. Кастомный скрипт склейки (генерируется на лету)
  set(GLUE_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/gen_unity_${TARGET_NAME}.cmake")
  file(WRITE "${GLUE_SCRIPT}" "
        file(GLOB ALL_C_FILES \"${OUTPUT_DIR}/*.c\")
        file(WRITE \"${UNITY_SOURCE_FILE}\" \"/* Автоматический Unity-сборник для ${TARGET_NAME} */\\n\")
        foreach(C_FILE IN LISTS ALL_C_FILES)
            if(NOT \"\${C_FILE}\" MATCHES \"asn1_unity_build.c$\" AND NOT \"\${C_FILE}\" MATCHES \"converter-example.c$\")
                file(APPEND \"${UNITY_SOURCE_FILE}\" \"#include \\\"\${C_FILE}\\\"\\n\")
            endif()
        endforeach()
    ")

  # 4. Кастомная команда: убрали WORKING_DIRECTORY, добавили флаг -D для asn1c
  add_custom_command(
        OUTPUT "${UNITY_SOURCE_FILE}"
        # Сначала гарантированно создаем директорию
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
        # Запуск компилятора ASN.1 с явным указанием выходной папки через -D
        COMMAND ${ASN1C_EXECUTABLE} -pdu=all -fcompound-names -gen-PER -D "${OUTPUT_DIR}" "${ABSOLUTE_ASN_FILE}"
        # Вычищаем мусорный пример
        COMMAND ${CMAKE_COMMAND} -E remove -f "${OUTPUT_DIR}/converter-example.c"
        # Запуск генератора склейки
        COMMAND ${CMAKE_COMMAND} -P "${GLUE_SCRIPT}"
        DEPENDS "${ABSOLUTE_ASN_FILE}"
        COMMENT "[ASN.1] Компиляция спецификации для таргета ${TARGET_NAME}..."
    )

  # 5. Создаем изолированную статическую библиотеку автогена
  add_library(${TARGET_NAME} STATIC "${UNITY_SOURCE_FILE}")

  # Оформляем API таргета
  target_include_directories(${TARGET_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${OUTPUT_DIR}>
    )

  # Глушим варнинги и прокидываем системные инклуды типов
  target_compile_options(${TARGET_NAME} PRIVATE -w ${ASN1C_COMPAT_FLAGS})
  target_compile_definitions(${TARGET_NAME} PUBLIC ASN_PDU_COLLECTION=1)
  set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

endfunction()
