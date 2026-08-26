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
# including fragment would resolve against tests/ instead. Bracketing every include is what makes
# the folder name reliable: a fragment cannot forget to set it and cannot leave a stale one behind
# for its successor, because neither is the fragment's job.
macro(nucleus_test_fragment folder)
    set(nucleus_test_dir ${folder})
    include(${folder}/tests.cmake)
    unset(nucleus_test_dir)
endmacro()

function(nucleus_test_sources out_var name)
    set(sources "")
    foreach(source IN LISTS ARGN)
        list(APPEND sources ${nucleus_test_dir}/${source})
    endforeach()
    if(NOT sources)
        set(sources ${nucleus_test_dir}/${name}.cpp)
    endif()
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

# A keyword written with no values after it parses as absent, which would silently substitute the
# single-source default for SOURCES and drop PROPERTIES altogether; a misspelled keyword parses as
# a trailing argument and would be discarded just as quietly.
function(nucleus_reject_bad_test_arguments name missing unparsed)
    if(NOT DEFINED nucleus_test_dir)
        message(FATAL_ERROR "nucleus_add_test(${name}): called outside nucleus_test_fragment()")
    endif()
    if(missing)
        message(FATAL_ERROR "nucleus_add_test(${name}): keyword(s) given no value: ${missing}")
    endif()
    if(unparsed)
        message(FATAL_ERROR "nucleus_add_test(${name}): unrecognized argument(s): ${unparsed}")
    endif()
endfunction()

# The core's private tree and tests/ itself are on every target's include path, so isolation tests
# can construct the registries directly and a quoted include written as a path from tests/ resolves
# from any folder at any depth.
function(nucleus_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINK;INCLUDE;DEFINES;PROPERTIES")
    nucleus_reject_bad_test_arguments(${name}
        "${arg_KEYWORDS_MISSING_VALUES}" "${arg_UNPARSED_ARGUMENTS}")
    nucleus_test_sources(sources ${name} ${arg_SOURCES})
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE nucleus::nucleus ${arg_LINK} Catch2::Catch2WithMain)
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

nucleus_test_fragment(resolve)
nucleus_test_fragment(utility)
nucleus_test_fragment(diagnostics)
nucleus_test_fragment(registry)
nucleus_test_fragment(env)
nucleus_test_fragment(error)
nucleus_test_fragment(config_node)
nucleus_test_fragment(completion)
nucleus_test_fragment(capability)
nucleus_test_fragment(facade)
nucleus_test_fragment(argv)
nucleus_test_fragment(runtime)
nucleus_test_fragment(config_source)
nucleus_test_fragment(query)
nucleus_test_fragment(tokenizer)
nucleus_test_fragment(schema)
nucleus_test_fragment(emit)
nucleus_test_fragment(keyspace)
nucleus_test_fragment(identity)
nucleus_test_fragment(compile_refusal)
nucleus_test_fragment(sanitizer_proof)
nucleus_test_fragment(core_purity)
nucleus_test_fragment(public_surface)
nucleus_test_fragment(consumer_surface)

if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_test_fragment(system)
    nucleus_test_fragment(collection_shapes)
    nucleus_test_fragment(xml)
endif()

# Last on purpose: every statement in this fragment names a target an earlier fragment registered.
nucleus_test_fragment(example_contracts)
