# Test framework: Catch2. Fetched via FetchContent by default so the walking
# skeleton builds anywhere; a vcpkg/find_package path can replace this later
# (scaffold default -- confirm during the project's tooling decision).
if(NUCLEUS_FETCH_TEST_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.0
    )
    FetchContent_MakeAvailable(Catch2)
else()
    find_package(Catch2 3 REQUIRED)
endif()

# Registers a Catch2 test executable built from a single source file, linked
# against the core library and Catch2's bundled main, and wired into CTest. The
# internal src/ tree is on the include path so isolation tests can construct the
# registries directly.
function(nucleus_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus Catch2::Catch2WithMain)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nucleus_add_test(version_test)
nucleus_add_test(result_test)
nucleus_add_test(format_test)
nucleus_add_test(identity_test)
nucleus_add_test(log_sink_test)
nucleus_add_test(facade_test)
nucleus_add_test(registry_isolation_test)
nucleus_add_test(source_seam_test)
nucleus_add_test(capability_gating_test)
nucleus_add_test(discovery_test)

# ---------------------------------------------------------------------------
# Buffer-lifetime test: exercises the XML document source through the seam and
# proves the copy-out severs every view from the document arena before it is
# dropped. Links the XML module (and thus pugixml, transitively) only here, never
# in core. Built only when the XML module is. Run under the sanitizer to make the
# use-after-free guard real.
# ---------------------------------------------------------------------------
if(NUCLEUS_BUILD_SOURCE_XML)
    add_executable(buffer_lifetime_test buffer_lifetime_test.cpp)
    target_link_libraries(buffer_lifetime_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(buffer_lifetime_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    add_test(NAME buffer_lifetime_test COMMAND buffer_lifetime_test)
endif()

# ---------------------------------------------------------------------------
# Negative compile test: a registry that stores a sibling reference member
# violates the flat-ownership invariant and must NOT compile. WILL_FAIL asserts
# the build of the fixture fails, making the invariant executable rather than
# review-only.
# ---------------------------------------------------------------------------
add_test(
    NAME flat_ownership_negative_compile
    COMMAND ${CMAKE_COMMAND} --build . --target sibling_member_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(flat_ownership_negative_compile PROPERTIES WILL_FAIL TRUE)

add_executable(sibling_member_fixture EXCLUDE_FROM_ALL
    isolation/sibling_member_fixture.cpp)
target_link_libraries(sibling_member_fixture PRIVATE nucleus::nucleus)
target_include_directories(sibling_member_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Core-purity check: the core must carry no format/host vocabulary. Greps the
# core headers/sources for xml/pugi/host symbols; nonzero hits fail the test.
# ---------------------------------------------------------------------------
add_test(
    NAME core_purity_check
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/scripts/core_purity_check.cmake
)

# ---------------------------------------------------------------------------
# ASan trip test: built and registered ONLY when the sanitizer is enabled. It
# deliberately reads freed heap memory to confirm ASan is wired and tripping; it
# is never part of a normal (non-sanitizer) run.
# ---------------------------------------------------------------------------
if(NUCLEUS_BUILD_SANITIZER)
    add_executable(asan_trip_test asan_trip_test.cpp)
    target_link_libraries(asan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME asan_trip_test COMMAND asan_trip_test)
    set_tests_properties(asan_trip_test PROPERTIES
        PASS_REGULAR_EXPRESSION "heap-use-after-free|AddressSanitizer")
endif()
