include_guard(GLOBAL)

function(register_asn1_decoder TECH_NAME)
  set(options "")
  set(oneValueArgs CONDITION DEFINE ASN_FILE SOLIB_FILE)
  set(multiValueArgs "")
  cmake_parse_arguments(ASN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(ASN_CONDITION)
    if(NOT ${ASN_CONDITION})
      return()
    endif()
  else()
    return()
  endif()

  set(STATIC_LIB "observer_asn1_${TECH_NAME}")
  set(SHARED_LIB "observer_solib_${TECH_NAME}")
  set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/asn1c_${TECH_NAME}")

  set(ASN_FILE_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_ASN_FILE}")
  set(SOLIB_PATH   "${CMAKE_CURRENT_SOURCE_DIR}/${ASN_SOLIB_FILE}")

  # 1. Запускаем генератор (исправленный, честный список файлов)
  add_asn1_library(${STATIC_LIB} "${ASN_FILE_ABS}" "${GEN_DIR}")

  # ПОЧИНЕНО: Больше НЕ линкуем PUBLIC в observer! Убираем target_link_libraries(observer PUBLIC ${STATIC_LIB})
  # Экспортируем только дефайн, чтобы ядро знало, что парсер доступен
  target_compile_definitions(observer PUBLIC "${ASN_DEFINE}=1")

  # 2. Создаем изолированную сошку для конкретной RAT (LTE / UMTS / 5G)
  add_library(${SHARED_LIB} SHARED "${SOLIB_PATH}")

  # Сошка забирает символы из своей статики и ядра, но наружу их не отдает
  target_link_libraries(${SHARED_LIB} PRIVATE ${STATIC_LIB} observer)
  target_include_directories(${SHARED_LIB} PRIVATE "${GEN_DIR}")

  # ЖЕСТКАЯ ИЗОЛЯЦИЯ СИМВОЛОВ: Глушим варнинги и полностью скрываем все внутренние
  # таблицы символов автогена внутри этой .so. Теперь сошка LTE никогда не узнает про типы из сошки 5G.
  target_compile_options(${SHARED_LIB} PRIVATE -w -fvisibility=hidden)
  target_link_options(${SHARED_LIB} PRIVATE -Wl,--exclude-libs,ALL -Wl,-Bsymbolic)

  set_target_properties(${SHARED_LIB} PROPERTIES
      OUTPUT_NAME "diag_${TECH_NAME}_rrc"
      PREFIX "lib"
      SUFFIX ".so"
  )

  message(STATUS "[ASN.1] Изолированная сошка сконфигурирована: libdiag_${TECH_NAME}_rrc.so")
endfunction()
