include("${CMAKE_CURRENT_LIST_DIR}/BSSASHelpers.cmake")

foreach(required_var IN ITEMS BSSAS_BUILD_CONFIG BSSAS_BINARY_DIR BSSAS_DISTRIBUTION_DIR)
    bssas_require_defined(${required_var})
endforeach()

string(TOUPPER "${BSSAS_BUILD_CONFIG}" bssas_build_config_upper)
if(NOT bssas_build_config_upper STREQUAL "RELEASE")
    message(STATUS "Skipping distribution generation for configuration: ${BSSAS_BUILD_CONFIG}")
    return()
endif()

message(STATUS "Rebuilding distribution directory: ${BSSAS_DISTRIBUTION_DIR}")
file(REMOVE_RECURSE "${BSSAS_DISTRIBUTION_DIR}")
file(MAKE_DIRECTORY "${BSSAS_DISTRIBUTION_DIR}")

set(install_command
    "${CMAKE_COMMAND}"
    --install "${BSSAS_BINARY_DIR}"
    --prefix "${BSSAS_DISTRIBUTION_DIR}"
)

if(NOT "${BSSAS_BUILD_CONFIG}" STREQUAL "")
    list(APPEND install_command --config "${BSSAS_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_result
)

if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Distribution generation failed with exit code ${install_result}")
endif()
