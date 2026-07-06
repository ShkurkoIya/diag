include_guard(GLOBAL)

function(register_rat_module MODULE_NAME)
  set(options "")
  set(oneValueArgs CONDITION DEFINE)
  set(multiValueArgs SOURCES)
  cmake_parse_arguments(RAT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Безопасное вычисление условий
  if(RAT_CONDITION)
    if(NOT ${RAT_CONDITION})
      return()
    endif()
  else()
    return()
  endif()

  # ПОЧИНЕНО ДЛЯ ПЕРЕНОСИМОСТИ: Переводим пути исходников софта в абсолютные
  set(ABSOLUTE_SOURCES "")
  foreach(SOURCE_FILE IN LISTS RAT_SOURCES)
    if(IS_ABSOLUTE "${SOURCE_FILE}")
      list(APPEND ABSOLUTE_SOURCES "${SOURCE_FILE}")
    else()
      list(APPEND ABSOLUTE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
    endif()
  endforeach()

  # Регистрируем исходники в ядро библиотеки
  target_sources(observer PRIVATE ${ABSOLUTE_SOURCES})

  # Добавляем приватную директорию инклудов текущей технологии
  target_include_directories(observer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}")

  # Экспортируем публичный макрос сборки модуля
  target_compile_definitions(observer PUBLIC "${RAT_DEFINE}=1")

  message(STATUS "[Module] Успешно добавлен сетевой стек: ${MODULE_NAME} (${RAT_DEFINE})")
endfunction()
