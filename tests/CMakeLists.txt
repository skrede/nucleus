# Test framework: Catch2. Fetched via FetchContent by default so the walking
# skeleton builds anywhere; a vcpkg/find_package path can replace this later
# (scaffold default -- confirm during the project's tooling decision).
if(NUCLEUS_FETCH_TEST_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        # v3.15.0, pinned to the commit so a moved tag cannot change the build
        GIT_TAG 6ee0826dcae55ed1e06b2c5701981221e979e1e6
        # SYSTEM: Catch2's headers are consumed as system includes so their own
        # diagnostics (e.g. the float compare inside the REQUIRE decomposer) do
        # not surface under our curated -Weverything/-Werror in test TUs.
        SYSTEM
    )
    FetchContent_MakeAvailable(Catch2)
else()
    find_package(Catch2 3 REQUIRED)
endif()

# include() keeps one directory scope for the whole test tree, so a source path relative to the
# including fragment would resolve against tests/ instead. Each fragment names its own folder in
# nucleus_test_dir and unsets it at the end, so a fragment that forgets to set it trips the fatal
# error below rather than silently inheriting its predecessor's folder.
function(nucleus_test_sources out_var name)
    if(NOT DEFINED nucleus_test_dir)
        message(FATAL_ERROR "nucleus_add_test(${name}): the including fragment set no nucleus_test_dir")
    endif()
    set(sources "")
    foreach(source IN LISTS ARGN)
        list(APPEND sources ${nucleus_test_dir}/${source})
    endforeach()
    if(NOT sources)
        set(sources ${nucleus_test_dir}/${name}.cpp)
    endif()
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

# With no keyword present every trailing argument lands in arg_UNPARSED_ARGUMENTS, so the bare
# form still means "link these libraries". The core's private tree and tests/ itself are on every
# target's include path, so isolation tests can construct the registries directly and a quoted
# include written as a path from tests/ resolves from any folder at any depth.
function(nucleus_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINK;INCLUDE;DEFINES;PROPERTIES")
    nucleus_test_sources(sources ${name} ${arg_SOURCES})
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE
        nucleus::nucleus ${arg_LINK} ${arg_UNPARSED_ARGUMENTS} Catch2::Catch2WithMain)
    nucleus_enable_coverage(${name})
    nucleus_warnings(${name})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${arg_INCLUDE})
    if(arg_DEFINES)
        target_compile_definitions(${name} PRIVATE ${arg_DEFINES})
    endif()
    add_test(NAME ${name} COMMAND ${name})
    if(arg_PROPERTIES)
        set_tests_properties(${name} PROPERTIES ${arg_PROPERTIES})
    endif()
endfunction()

include(resolve/tests.cmake)
include(utility/tests.cmake)
include(diagnostics/tests.cmake)
include(registry/tests.cmake)
include(env/tests.cmake)
include(error/tests.cmake)
include(config_node/tests.cmake)
include(completion/tests.cmake)
include(capability/tests.cmake)
include(facade/tests.cmake)
include(argv/tests.cmake)
include(runtime/tests.cmake)
include(config_source/tests.cmake)
include(query/tests.cmake)
include(tokenizer/tests.cmake)
include(schema/tests.cmake)
include(emit/tests.cmake)
include(keyspace/tests.cmake)
include(identity/tests.cmake)
include(compile_refusal/tests.cmake)
include(sanitizer_proof/tests.cmake)
include(core_purity/tests.cmake)

if(NUCLEUS_BUILD_SOURCE_XML)
    include(system/tests.cmake)
    include(collection_shapes/tests.cmake)
    include(xml/tests.cmake)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/example_review_tests.cmake)

# ---------------------------------------------------------------------------
# NUCLEUS_WERROR top-level-only default: proves nucleus_IS_TOP_LEVEL-derived
# default resolves OFF for a nested add_subdirectory() consumer whose own
# project() call precedes nucleus's. Unconditional (not clang-gated) since the
# check is compiler-independent. A bare "configure succeeds" check would be
# insufficient -- it must also confirm the SPECIFIC cache value, not merely
# that configure didn't crash.
# ---------------------------------------------------------------------------
add_test(
    NAME nested_consumer_werror_default_off
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/nested_consumer_fixture
        -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/nested_consumer_fixture_build
        -P ${CMAKE_SOURCE_DIR}/scripts/expect_nested_consumer_werror_off.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ---------------------------------------------------------------------------
# Vendoring-consumer install gate: a project that vendors nucleus and installs
# an executable of its own must not be forced to leave a transitive dependency's
# install rules enabled to satisfy an export set that dependency is not in. A
# bare "configure succeeds" check would be insufficient -- it must confirm the
# SPECIFIC resolved cache value. Linux-only: the claim is about CMake option
# resolution, and the entry costs a second nucleus configure plus a dependency
# population no other platform's suite run should pay for.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Reuse a dependency source the outer build already populated rather than
    # cloning a second copy; with none on disk the script populates its own.
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
            -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/install_consumer_fixture
            -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_build
            -DNUCLEUS_FIXTURE_PREFIX_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_prefix
            ${nucleus_fixture_args}
            -P ${CMAKE_SOURCE_DIR}/scripts/expect_install_off.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    # A nested configure, a dependency population and a consumer build are not a
    # unit test's budget, and a per-test TIMEOUT outranks ctest --timeout.
    set_tests_properties(nested_consumer_install_default_off PROPERTIES TIMEOUT 1200)
endif()

# ---------------------------------------------------------------------------
# Public-surface gate: a generated TU includes EVERY public header and compiles
# with ONLY the public include roots on the path -- no lib/*/src anywhere. This
# is the installed consumer's view of the library; a public header that reaches
# into a private one breaks this target even though every other test (which has
# lib/core/src on its include path) keeps compiling.
# ---------------------------------------------------------------------------
set(public_surface_roots
    ${CMAKE_SOURCE_DIR}/lib/core/include
    ${CMAKE_SOURCE_DIR}/lib/env/include
    ${CMAKE_SOURCE_DIR}/lib/argv/include
    ${CMAKE_SOURCE_DIR}/lib/runtime/include)
if(NUCLEUS_BUILD_SOURCE_XML)
    list(APPEND public_surface_roots ${CMAKE_SOURCE_DIR}/lib/xml/include)
endif()

set(public_surface_tu "")
foreach(root ${public_surface_roots})
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS RELATIVE ${root} ${root}/nucleus/*.h)
    list(SORT public_headers)
    foreach(header ${public_headers})
        string(APPEND public_surface_tu "#include <${header}>\n")
    endforeach()
endforeach()
string(APPEND public_surface_tu "\nint main() {}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/public_surface_test.cpp "${public_surface_tu}")

add_executable(public_surface_test ${CMAKE_CURRENT_BINARY_DIR}/public_surface_test.cpp)
target_link_libraries(public_surface_test
    PRIVATE nucleus::nucleus nucleus::env nucleus::argv nucleus::runtime)
if(NUCLEUS_BUILD_SOURCE_XML)
    target_link_libraries(public_surface_test PRIVATE nucleus::xml)
endif()
add_test(NAME public_surface_test COMMAND public_surface_test)
