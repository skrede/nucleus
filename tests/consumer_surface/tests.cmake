function(nucleus_fixture_source out name deps)
    string(TOUPPER ${name} upper)
    set(source ${FETCHCONTENT_SOURCE_DIR_${upper}})
    if(NOT source AND EXISTS ${deps}/${name}-src)
        set(source ${deps}/${name}-src)
    endif()
    set(${out} ${source} PARENT_SCOPE)
endfunction()

function(nucleus_fixture_skip_reason out deps)
    if(NOT NUCLEUS_BUILD_SOURCE_XML)
        set(reason "the outer build has NUCLEUS_BUILD_SOURCE_XML OFF, so it populated no pugixml to hand on")
    elseif(pugixml_DIR)
        set(reason "the outer build resolved pugixml from the package at ${pugixml_DIR}, so it populated no source to hand on")
    else()
        set(reason "the outer build left no pugixml source under ${deps} to hand on")
    endif()
    set(${out} "${reason}" PARENT_SCOPE)
endfunction()

# The nested configures reuse the dependency sources the outer build populated, because ctest
# must not need the network.
set(nucleus_fixture_deps ${CMAKE_BINARY_DIR}/_deps)
if(FETCHCONTENT_BASE_DIR)
    set(nucleus_fixture_deps ${FETCHCONTENT_BASE_DIR})
endif()
nucleus_fixture_source(nucleus_fixture_fmt fmt ${nucleus_fixture_deps})
set(nucleus_fixture_fmt_args)
if(nucleus_fixture_fmt)
    set(nucleus_fixture_fmt_args -DNUCLEUS_FIXTURE_FMT_SOURCE_DIR=${nucleus_fixture_fmt})
endif()

# Proves the top-level-derived NUCLEUS_WERROR default resolves OFF for a nested consumer whose own
# project() call precedes nucleus's. A bare "configure succeeds" check would be insufficient: the
# specific resolved cache value is the claim.
add_test(
    NAME nested_consumer_werror_default_off
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/consumer_surface/nested_consumer_fixture
        -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/nested_consumer_fixture_build
        ${nucleus_fixture_fmt_args}
        -P ${CMAKE_SOURCE_DIR}/scripts/expect_nested_consumer_werror_off.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Proves a vendoring consumer that installs an executable of its own is not forced to keep a
# transitive dependency's install rules enabled. Linux-only: the claim is about CMake option
# resolution, and the entry costs a second nucleus configure plus a dependency population no other
# platform's suite run should pay for. Here too the specific resolved cache value is the claim.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # With no pugixml population to hand on, the gate is skipped and names why.
    nucleus_fixture_source(nucleus_fixture_pugixml pugixml ${nucleus_fixture_deps})
    if(nucleus_fixture_pugixml)
        set(nucleus_fixture_args -DNUCLEUS_FIXTURE_PUGIXML_SOURCE_DIR=${nucleus_fixture_pugixml})
    else()
        nucleus_fixture_skip_reason(nucleus_fixture_reason ${nucleus_fixture_deps})
        set(nucleus_fixture_args "-DNUCLEUS_FIXTURE_SKIP_REASON=${nucleus_fixture_reason}")
    endif()
    add_test(
        NAME nested_consumer_install_default_off
        COMMAND ${CMAKE_COMMAND}
            -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/consumer_surface/install_consumer_fixture
            -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_build
            -DNUCLEUS_FIXTURE_PREFIX_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_prefix
            ${nucleus_fixture_args}
            ${nucleus_fixture_fmt_args}
            -P ${CMAKE_SOURCE_DIR}/scripts/expect_install_off.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    # A nested configure, a dependency population and a consumer build are not a unit test's
    # budget, and a per-test TIMEOUT outranks ctest --timeout.
    set_tests_properties(nested_consumer_install_default_off PROPERTIES TIMEOUT 1200
        SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "install gate check skipped: ")
endif()
