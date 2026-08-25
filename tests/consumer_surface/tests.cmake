# Proves the top-level-derived NUCLEUS_WERROR default resolves OFF for a nested consumer whose own
# project() call precedes nucleus's. A bare "configure succeeds" check would be insufficient: the
# specific resolved cache value is the claim.
add_test(
    NAME nested_consumer_werror_default_off
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/consumer_surface/nested_consumer_fixture
        -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/nested_consumer_fixture_build
        -P ${CMAKE_SOURCE_DIR}/scripts/expect_nested_consumer_werror_off.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Proves a vendoring consumer that installs an executable of its own is not forced to keep a
# transitive dependency's install rules enabled. Linux-only: the claim is about CMake option
# resolution, and the entry costs a second nucleus configure plus a dependency population no other
# platform's suite run should pay for. Here too the specific resolved cache value is the claim.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Reuse a dependency source the outer build already populated rather than cloning a second
    # copy; with none on disk the script populates its own.
    set(nucleus_fixture_deps ${CMAKE_BINARY_DIR}/_deps)
    if(FETCHCONTENT_BASE_DIR)
        set(nucleus_fixture_deps ${FETCHCONTENT_BASE_DIR})
    endif()
    set(nucleus_fixture_args)
    if(EXISTS ${nucleus_fixture_deps}/pugixml-src)
        set(nucleus_fixture_args
            -DNUCLEUS_FIXTURE_PUGIXML_SOURCE_DIR=${nucleus_fixture_deps}/pugixml-src)
    endif()
    add_test(
        NAME nested_consumer_install_default_off
        COMMAND ${CMAKE_COMMAND}
            -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/consumer_surface/install_consumer_fixture
            -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_build
            -DNUCLEUS_FIXTURE_PREFIX_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_prefix
            ${nucleus_fixture_args}
            -P ${CMAKE_SOURCE_DIR}/scripts/expect_install_off.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    # A nested configure, a dependency population and a consumer build are not a unit test's
    # budget, and a per-test TIMEOUT outranks ctest --timeout.
    set_tests_properties(nested_consumer_install_default_off PROPERTIES TIMEOUT 1200)
endif()
