# ─────────────────────────────────────────────────────────────────────────────
# Функция декларативной регистрации RAT
# ─────────────────────────────────────────────────────────────────────────────
function(register_rat_module MODULE_NAME)
  set(options "")
  set(oneValueArgs CONDITION DEFINE)
  set(multiValueArgs SOURCES)
  cmake_parse_arguments(RAT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(${RAT_CONDITION})
    target_sources(${PROJECT_NAME} PRIVATE ${RAT_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}")
    target_compile_definitions(${PROJECT_NAME} PUBLIC "${RAT_DEFINE}=1")
  endif()
endfunction()
