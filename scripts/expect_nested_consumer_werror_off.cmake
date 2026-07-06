# NUCLEUS_WERROR top-level-only default assertion.
#
# Configures the nested_consumer_fixture (a standalone project whose own
# project() call precedes nucleus's, add_subdirectory()-ing the real nucleus
# tree) and asserts the resulting cache resolves NUCLEUS_WERROR to OFF. A bare
# "did configure succeed" check would be insufficient on its own: it would not
# catch NUCLEUS_WERROR silently defaulting back to ON for a nested consumer,
# which is the exact regression this test exists to prevent.
#
# Invoke: cmake -DNUCLEUS_FIXTURE_SOURCE_DIR=<dir> -DNUCLEUS_FIXTURE_BINARY_DIR=<dir>
#               -P scripts/expect_nested_consumer_werror_off.cmake

foreach(required NUCLEUS_FIXTURE_SOURCE_DIR NUCLEUS_FIXTURE_BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${NUCLEUS_FIXTURE_SOURCE_DIR} -B ${NUCLEUS_FIXTURE_BINARY_DIR}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE  configure_stderr)

if(NOT configure_result EQUAL 0)
    message("${configure_stdout}${configure_stderr}")
    message(FATAL_ERROR "nested consumer fixture failed to configure")
endif()

file(STRINGS "${NUCLEUS_FIXTURE_BINARY_DIR}/CMakeCache.txt" werror_lines
    REGEX "^NUCLEUS_WERROR:BOOL=")

list(LENGTH werror_lines werror_line_count)
if(NOT werror_line_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one NUCLEUS_WERROR:BOOL= cache entry, found ${werror_line_count}")
endif()

list(GET werror_lines 0 werror_line)
if(NOT werror_line STREQUAL "NUCLEUS_WERROR:BOOL=OFF")
    message(FATAL_ERROR
        "NUCLEUS_WERROR did not resolve OFF for the nested consumer: found '${werror_line}'")
endif()

message(STATUS
    "nested consumer check: NUCLEUS_WERROR resolves OFF for a nested add_subdirectory() consumer as expected")
