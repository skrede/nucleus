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
    keyref pkey_identity query tree_references
    constraint_groups keyed_composition schema strains typed
    capability_gating custom_source env
    parser_concept registration_policy
    pkey_tokenizer time_tokenizer tokens
    emit_template round_trip xml xml_persist)
list(LENGTH example_contract_targets example_contract_count)
if(NOT example_contract_count EQUAL 33)
    message(FATAL_ERROR "example contract manifest must contain exactly 33 targets")
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
