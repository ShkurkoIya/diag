# Функция для полной инкапсуляции и сборки тяжелого ASN.1 автогена и сошек
function(register_asn1_decoder TECH_NAME)
  set(options "")
  set(oneValueArgs CONDITION DEFINE ASN_FILE SOLIB_FILE)
  set(multiValueArgs "")
  cmake_parse_arguments(ASN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(${ASN_CONDITION})
    set(STATIC_LIB "rrc_asn1_${TECH_NAME}")
    set(SHARED_LIB "diag_${TECH_NAME}_rrc")
    set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/asn1c_${TECH_NAME}")

    # 1. Генерируем статическую библиотеку автогена из .asn спецификации
    add_asn1_library(${STATIC_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_ASN_FILE}" "${GEN_DIR}")

    # 2. Инкапсулируем её внутри главного ядра observer
    target_link_libraries(${PROJECT_NAME} PUBLIC ${STATIC_LIB})
    target_compile_definitions(${PROJECT_NAME} PUBLIC "${ASN_DEFINE}=1")

    # 3. Автоматически собираем изолированный Shared Object (для Android/Kotlin)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_SOLIB_FILE}")
      add_library(${SHARED_LIB} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_SOLIB_FILE}")
      target_link_libraries(${SHARED_LIB} PRIVATE ${STATIC_LIB} ${PROJECT_NAME})
      target_include_directories(${SHARED_LIB} PRIVATE "${GEN_DIR}")

      # Строгие правила скрытия кишок API разделяемой библиотеки
      target_compile_options(${SHARED_LIB} PRIVATE -w -fvisibility=hidden)
      target_link_options(${SHARED_LIB} PRIVATE -Wl,--exclude-libs,ALL)
      set_target_properties(${SHARED_LIB} PROPERTIES OUTPUT_NAME "${SHARED_LIB}" PREFIX "lib" SUFFIX ".so")
    endif()
  endif()
endfunction()
