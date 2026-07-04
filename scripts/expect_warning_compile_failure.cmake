# Negative-compile assertion with a pinned diagnostic.
#
# Drives a build of a single fixture target that is expected to fail, then
# asserts TWO things rather than the weaker "it failed somehow":
#   1. the build returned nonzero (the gate bit), and
#   2. the compiler output actually contains the targeted diagnostic.
# This distinguishes a genuine trip of the intended warning from an unrelated
# compile break (a fixture typo, a transitive dependency error, or a different
# warning promoted by -Werror), any of which would otherwise turn a bare
# WILL_FAIL test green for the wrong reason. It mirrors the sanitizer trip
# tests, which pin their cause via PASS_REGULAR_EXPRESSION.
#
# Invoke: cmake -DNUCLEUS_BUILD_DIR=<dir> -DNUCLEUS_TARGET=<target>
#               -DNUCLEUS_CONFIG=<cfg> -DNUCLEUS_EXPECT=<substring>
#               -P scripts/expect_warning_compile_failure.cmake

foreach(required NUCLEUS_BUILD_DIR NUCLEUS_TARGET NUCLEUS_EXPECT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

set(build_command
    "${CMAKE_COMMAND}" --build "${NUCLEUS_BUILD_DIR}" --target "${NUCLEUS_TARGET}")
if(DEFINED NUCLEUS_CONFIG AND NOT NUCLEUS_CONFIG STREQUAL "")
    list(APPEND build_command --config "${NUCLEUS_CONFIG}")
endif()

execute_process(
    COMMAND ${build_command}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE  build_stderr)

set(build_output "${build_stdout}${build_stderr}")

if(build_result EQUAL 0)
    message("${build_output}")
    message(FATAL_ERROR
        "target '${NUCLEUS_TARGET}' compiled cleanly, but the gate was expected "
        "to fail it on '${NUCLEUS_EXPECT}'")
endif()

string(FIND "${build_output}" "${NUCLEUS_EXPECT}" position)
if(position EQUAL -1)
    message("${build_output}")
    message(FATAL_ERROR
        "target '${NUCLEUS_TARGET}' failed to build, but not on the expected "
        "diagnostic '${NUCLEUS_EXPECT}' -- the build broke for a different reason")
endif()

message(STATUS
    "negative-compile check: '${NUCLEUS_TARGET}' failed on '${NUCLEUS_EXPECT}' as expected")
