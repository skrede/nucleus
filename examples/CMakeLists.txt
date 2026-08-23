# Example programs, built under -DNUCLEUS_BUILD_EXAMPLES=ON. Each is a small,
# self-contained main() focused on one concept, linked against the core library
# (and, where it needs them, the separately-linked source/tokenizer modules).
# They are demonstrations, not tests, so they are not registered with CTest.
#
# Examples are grouped by area into subdirectories; each subdirectory lists its
# own programs. The nucleus_add_example helper is defined here so every group
# reuses it without boilerplate.

# Registers an example executable linked against the core library. Additional
# link targets (e.g. nucleus::env, nucleus::argv) are passed as extra arguments.
# Relative source paths resolve against the calling subdirectory.
function(nucleus_add_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus ${ARGN})
    nucleus_warnings(${name})
endfunction()

add_subdirectory(basics)
add_subdirectory(sources)
add_subdirectory(composition)
add_subdirectory(cli)
add_subdirectory(tokens)
add_subdirectory(schema)
add_subdirectory(references)
add_subdirectory(xml)

set(example_contract_targets
    diagnostics logging quickstart
    argv argv_delimiter argv_recognizer cli_help completion
    layering plugin_spaces reusable_space source_stack
    keyref query tree_references
    constraint_groups keyed_composition schema
    capability_gating custom_source env
    parser_concept registration_policy
    pkey_tokenizer time_tokenizer tokens)
set(example_contract_expected 26)

if(NUCLEUS_BUILD_SOURCE_XML)
    list(APPEND example_contract_targets
        pkey_identity strains typed
        emit_template round_trip xml xml_persist)
    set(example_contract_expected 33)
endif()

list(LENGTH example_contract_targets example_contract_count)
if(NOT example_contract_count EQUAL example_contract_expected)
    message(FATAL_ERROR
        "example contract manifest must contain exactly ${example_contract_expected} targets")
endif()

set(example_contract_paths)
foreach(example_target IN LISTS example_contract_targets)
    if(NOT TARGET ${example_target})
        message(FATAL_ERROR "example contract target '${example_target}' does not exist")
    endif()
    list(APPEND example_contract_paths "$<TARGET_FILE:${example_target}>")
endforeach()

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/example-contract-targets-$<CONFIG>.cmake"
    CONTENT "set(NUCLEUS_EXAMPLE_TARGETS \"${example_contract_targets}\")\nset(NUCLEUS_EXAMPLE_PATHS \"${example_contract_paths}\")\n")
