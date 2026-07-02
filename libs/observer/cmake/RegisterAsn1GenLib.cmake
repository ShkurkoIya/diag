include_guard(GLOBAL)

function(register_asn1_decoder TECH_NAME)
  set(options "")
  set(oneValueArgs CONDITION DEFINE ASN_FILE SOLIB_FILE)
  set(multiValueArgs "")
  cmake_parse_arguments(ASN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(${ASN_CONDITION})
    set(STATIC_LIB "observer_asn1_${TECH_NAME}")
    set(SHARED_LIB "observer_solib_${TECH_NAME}")
    set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/asn1c_${TECH_NAME}")

    # Вызываем динамический генератор
    add_asn1_library(${STATIC_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_ASN_FILE}" "${GEN_DIR}")

    target_link_libraries(observer PUBLIC ${STATIC_LIB})
    target_compile_definitions(observer PUBLIC "${ASN_DEFINE}=1")

    set(SOLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_SOLIB_FILE}")
    if(EXISTS "${SOLIB_PATH}")
      add_library(${SHARED_LIB} SHARED "${SOLIB_PATH}")
      target_link_libraries(${SHARED_LIB} PRIVATE ${STATIC_LIB} observer)
      target_include_directories(${SHARED_LIB} PRIVATE "${GEN_DIR}")

      target_compile_options(${SHARED_LIB} PRIVATE -w -fvisibility=hidden)
      target_link_options(${SHARED_LIB} PRIVATE -Wl,--exclude-libs,ALL)

      set_target_properties(${SHARED_LIB} PROPERTIES
                OUTPUT_NAME "diag_${TECH_NAME}_rrc"
                PREFIX "lib"
                SUFFIX ".so"
            )
    endif()
  endif()
endfunction()
