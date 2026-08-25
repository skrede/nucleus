# The core must carry no format or host vocabulary: this greps the core's headers and sources for
# those symbols and for per-module adapter includes, and any hit fails the test.
add_test(
    NAME core_purity_check
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/scripts/core_purity_check.cmake
)
