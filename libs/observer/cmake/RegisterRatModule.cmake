include_guard(GLOBAL)

#[[
    Функция register_rat_module
    Декларативно регистрирует исходники конкретной технологии (GSM, LTE и т.д.)
    в общее ядро библиотеки observer.
]]
function(register_rat_module MODULE_NAME)
  set(options "")
  set(oneValueArgs CONDITION DEFINE)
  set(multiValueArgs SOURCES)
  cmake_parse_arguments(RAT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Оцениваем условие сборки (например, USE_GSM)
  if(${RAT_CONDITION})
    # Безопасно прокидываем исходники в основную библиотеку
    target_sources(observer PRIVATE ${RAT_SOURCES})

    # Добавляем приватную директорию инклудов текущей технологии
    target_include_directories(observer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}")

    # Экспортируем публичный макрос (FEATURE_GSM=1), чтобы внешние приложения знали, что модуль собран
    target_compile_definitions(observer PUBLIC "${RAT_DEFINE}=1")

    message(STATUS "[Module] Зарегистрирован сетевой стек: ${MODULE_NAME} (Флаг: ${RAT_DEFINE})")
  endif()
endfunction()
