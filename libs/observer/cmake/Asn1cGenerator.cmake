include_guard(GLOBAL)

function(add_asn1_library TARGET_NAME ASN_FILE OUTPUT_DIR)
  get_filename_component(ABSOLUTE_ASN_FILE "${ASN_FILE}" ABSOLUTE)
  set(UNITY_SOURCE_FILE "${OUTPUT_DIR}/asn1_unity_build.c")

  set(ASN1C_COMPAT_FLAGS "")
  if(NOT ANDROID)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      list(APPEND ASN1C_COMPAT_FLAGS "-include" "stddef.h" "-include" "sys/types.h" "-include" "signal.h")
    elseif(MSVC)
      list(APPEND ASN1C_COMPAT_FLAGS "/FIstddef.h" "/FIsys/types.h" "/FIsignal.h")
    endif()
  endif()

  set(GLUE_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/gen_unity_${TARGET_NAME}.cmake")
  file(WRITE "${GLUE_SCRIPT}" "
    execute_process(COMMAND \${CMAKE_COMMAND} -E sleep 0.1)

    file(GLOB ALL_C_FILES \"${OUTPUT_DIR}/*.c\")
    set(CONTENT \"/* Автоматический Unity-сборник для ${TARGET_NAME} */\\n\")

    # ПОЧИНЕНО: Добавлено обязательное ключевое слово LISTS
    foreach(C_FILE IN LISTS ALL_C_FILES)
        get_filename_component(FILE_NAME \"\${C_FILE}\" NAME)
        if(NOT \"\${FILE_NAME}\" STREQUAL \"asn1_unity_build.c\"
           AND NOT \"\${FILE_NAME}\" STREQUAL \"converter-example.c\"
           AND NOT \"\${FILE_NAME}\" STREQUAL \"pdu_collection.c\")
            string(APPEND CONTENT \"#include \\\"\${C_FILE}\\\"\\n\")
        endif()
    endforeach()

    file(WRITE \"${UNITY_SOURCE_FILE}\" \"\${CONTENT}\")
  ")

  add_custom_command(
        OUTPUT "${UNITY_SOURCE_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
        COMMAND "${CMAKE_BINARY_DIR}/external/asn1c/bin/asn1c" -pdu=all -fcompound-names -gen-UPER -gen-APER -D "${OUTPUT_DIR}" "${ABSOLUTE_ASN_FILE}"
        COMMAND ${CMAKE_COMMAND} -E remove -f "${OUTPUT_DIR}/converter-example.c"
        COMMAND ${CMAKE_COMMAND} -P "${GLUE_SCRIPT}"
        DEPENDS "${ABSOLUTE_ASN_FILE}"
        COMMENT "[ASN.1] Нативный сборник спецификации для ${TARGET_NAME}..."
    )

  add_library(${TARGET_NAME} STATIC "${UNITY_SOURCE_FILE}")

  if(TARGET asn1c_host_compiler)
    add_dependencies(${TARGET_NAME} asn1c_host_compiler)
  endif()

  target_include_directories(${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${OUTPUT_DIR}>)
  target_compile_options(${TARGET_NAME} PRIVATE -w ${ASN1C_COMPAT_FLAGS})
  target_compile_definitions(${TARGET_NAME} PUBLIC ASN_PDU_COLLECTION=1)
  set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
