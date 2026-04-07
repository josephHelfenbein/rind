if(NOT DEFINED BUILD_CONFIG OR NOT BUILD_CONFIG STREQUAL "Release")
  return()
endif()

if(NOT DEFINED SOURCE_FILE OR NOT EXISTS "${SOURCE_FILE}")
  return()
endif()

if(NOT DEFINED DEST_DIR)
  message(FATAL_ERROR "DEST_DIR is required")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE_FILE}" "${DEST_DIR}"
  RESULT_VARIABLE _copy_result
)

if(NOT _copy_result EQUAL 0)
  message(FATAL_ERROR "Failed to copy runtime file '${SOURCE_FILE}' to '${DEST_DIR}'")
endif()
