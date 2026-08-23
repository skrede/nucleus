cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS NUCLEUS_SOURCE_DIR NUCLEUS_BUILD_DIR NUCLEUS_CONFIG)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be set")
    endif()
endforeach()
get_filename_component(source_dir "${NUCLEUS_SOURCE_DIR}" ABSOLUTE)
get_filename_component(build_dir "${NUCLEUS_BUILD_DIR}" ABSOLUTE)

find_program(ctest_exe NAMES ctest REQUIRED)
find_program(format_exe NAMES clang-format REQUIRED)
find_program(git_exe NAMES git REQUIRED)

function(run_checked output label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    string(REPLACE "\r\n" "\n" stdout "${stdout}")
    string(REPLACE "\r\n" "\n" stderr "${stderr}")
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed with status ${status}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(${output} "${stdout}" PARENT_SCOPE)
endfunction()

function(assert_contains text expected label)
    string(FIND "${text}" "${expected}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${label} did not contain:\n${expected}\nactual:\n${text}")
    endif()
endfunction()

function(line_count path output)
    file(READ "${path}" content)
    string(REGEX MATCHALL "\n" newlines "${content}")
    list(LENGTH newlines lines)
    set(${output} ${lines} PARENT_SCOPE)
endfunction()

run_checked(configure_output "configure" ${CMAKE_COMMAND}
    -S "${source_dir}" -B "${build_dir}"
    -DCMAKE_CXX_STANDARD=20 -DCMAKE_BUILD_TYPE=${NUCLEUS_CONFIG}
    -DNUCLEUS_BUILD_TESTS=ON -DNUCLEUS_BUILD_EXAMPLES=ON
    -DNUCLEUS_BUILD_SOURCE_XML=ON -DNUCLEUS_WERROR=ON)
run_checked(build_output "build" ${CMAKE_COMMAND}
    --build "${build_dir}" --config "${NUCLEUS_CONFIG}" --parallel 2)

set(focused_tests "^(facade_schema_test|recognizer_of_test|auto_gating_test|selector_combinator_test|pkey_tokenizer_test|typed_element_test|constraint_group_test|registration_policy_test)$")
run_checked(focused_output "focused CTest" "${ctest_exe}"
    --test-dir "${build_dir}" -C "${NUCLEUS_CONFIG}"
    -R "${focused_tests}" --output-on-failure)
run_checked(complete_output "complete CTest" "${ctest_exe}"
    --test-dir "${build_dir}" -C "${NUCLEUS_CONFIG}" --output-on-failure)

set(expected_targets
    diagnostics logging quickstart argv argv_delimiter argv_recognizer cli_help completion
    layering plugin_spaces reusable_space source_stack keyref query tree_references
    constraint_groups keyed_composition schema capability_gating custom_source env
    parser_concept registration_policy pkey_tokenizer time_tokenizer tokens
    pkey_identity strains typed emit_template round_trip xml xml_persist)
include("${build_dir}/example-contract-targets-${NUCLEUS_CONFIG}.cmake")
list(LENGTH NUCLEUS_EXAMPLE_TARGETS target_count)
list(LENGTH NUCLEUS_EXAMPLE_PATHS path_count)
if(NOT target_count EQUAL 33 OR NOT path_count EQUAL 33)
    message(FATAL_ERROR "generated example manifest must contain 33 aligned targets and paths")
endif()
if(NOT "${NUCLEUS_EXAMPLE_TARGETS}" STREQUAL "${expected_targets}")
    message(FATAL_ERROR "generated example target identities differ from the expected manifest")
endif()

math(EXPR last_target "${target_count} - 1")
foreach(index RANGE 0 ${last_target})
    list(GET NUCLEUS_EXAMPLE_TARGETS ${index} target)
    list(GET NUCLEUS_EXAMPLE_PATHS ${index} path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "example target '${target}' resolved to missing path '${path}'")
    endif()
    run_checked(example_output "example '${target}' at '${path}'" "${path}")
    set(example_output_${target} "${example_output}")
endforeach()

assert_contains("${example_output_schema}" "schema_violation: schema validation failed:\n  - required field 'server/host' is missing" "schema contract")
assert_contains("${example_output_argv_recognizer}" "unknown CLI flag '--server-timeout=30' maps to undeclared key 'server/timeout'" "argv contract")
assert_contains("${example_output_capability_gating}" "unmet_capability: no source can satisfy capability 'nesting'" "capability contract")
assert_contains("${example_output_query}" "error: query matched 2 nodes; one() requires exactly one match" "query ambiguity contract")
set(query_owned "--- owned_by(network_owner) ---\n  cluster\n  cluster/server\n  cluster/server[0]\n  cluster/server[0]/host\n  cluster/server[0]/name\n  cluster/server[0]/port\n  cluster/server[1]\n  cluster/server[1]/host\n  cluster/server[1]/name\n  cluster/server[1]/port")
assert_contains("${example_output_query}" "${query_owned}" "query ownership contract")
assert_contains("${example_output_pkey_tokenizer}" "primary:   primary at 10.0.0.1:9000\nsecondary: secondary at 10.0.0.2:9000\nhost tok:  host: alpha at 10.0.1.1:9000" "primary-key tokenizer contract")
assert_contains("${example_output_typed}" "pos: 1, 2.5, 3\nmass: 42\npos (text): 1.0,2.5,3.0" "typed contract")
foreach(cue IN ITEMS "OK: configuration is valid" "requires at most 1 active member(s)" "is partially present (1 of 2)" "ttl must be greater than zero" "identifier 'name'='x' is not unique")
    assert_contains("${example_output_constraint_groups}" "${cue}" "constraint contract")
endforeach()
foreach(cue IN ITEMS "rogue plugin admitted: no" "cross-plugin conflicts: 0" "cache/policy = lru" "cache/size_mb = 256" "net/listen = 0.0.0.0:8080" "net/proto = tcp" "private net/listen = 0.0.0.0:8080" "private net/proto = tcp")
    assert_contains("${example_output_plugin_spaces}" "${cue}" "plugin contract")
endforeach()

set(cpp_files
    examples/cli/argv_recognizer.cpp examples/composition/plugin_spaces.cpp
    examples/references/query.cpp examples/schema/constraint_groups.cpp
    examples/schema/schema.cpp examples/schema/typed.cpp
    examples/sources/capability_gating.cpp examples/tokens/pkey_tokenizer.cpp
    lib/core/include/nucleus/tokenizer/tree_tokenizer.h
    tests/example_argv_recognizer_contract_test.cpp tests/example_capability_gating_contract_test.cpp
    tests/example_constraint_groups_contract_test.cpp tests/example_pkey_tokenizer_contract_test.cpp
    tests/example_plugin_spaces_contract_test.cpp tests/example_query_contract_test.cpp
    tests/example_schema_contract_test.cpp tests/example_typed_contract_test.cpp tests/pkey_tokenizer_test.cpp)
set(format_files)
foreach(file IN LISTS cpp_files)
    list(APPEND format_files "${source_dir}/${file}")
endforeach()
run_checked(format_output "clang-format" "${format_exe}" --dry-run --Werror ${format_files})
set(size_files ${format_files} "${source_dir}/examples/CMakeLists.txt" "${source_dir}/tests/cmake/check_example_contracts.cmake")
run_checked(size_output "local size gate" ${CMAKE_COMMAND}
    "-DNUCLEUS_SIZE_FILES=${size_files}" -P "${source_dir}/tests/cmake/check_local_size_growth.cmake")

# One-sided on purpose: the exception ledger holds this unit's decomposition for the
# test-tree reorganization, so a smaller manifest must not fail the gate.
line_count("${source_dir}/tests/CMakeLists.txt" manifest_lines)
if(manifest_lines GREATER 683)
    message(FATAL_ERROR
        "tests/CMakeLists.txt grew past its recorded ceiling: ${manifest_lines} lines, ceiling 683")
endif()

file(GLOB_RECURSE test_files RELATIVE "${source_dir}" "${source_dir}/tests/*.h" "${source_dir}/tests/*.cpp")
set(measured_rows)
foreach(file IN LISTS test_files)
    line_count("${source_dir}/${file}" lines)
    if(lines GREATER 200)
        math(EXPR rank "999999 - ${lines}")
        string(LENGTH "${rank}" digits)
        while(digits LESS 6)
            string(PREPEND rank "0")
            math(EXPR digits "${digits} + 1")
        endwhile()
        list(APPEND measured_rows "${rank}|${file}|${lines}")
    endif()
endforeach()
list(SORT measured_rows)
set(measured_ledger)
foreach(row IN LISTS measured_rows)
    string(REGEX REPLACE "^[0-9]+\\|([^|]+)\\|([0-9]+)$" "\\1 \\2" row "${row}")
    list(APPEND measured_ledger "${row}")
endforeach()

file(READ "${source_dir}/EXCEPTIONS.md" exceptions)
string(FIND "${exceptions}" "## Carried — test files over the 200-line ceiling" ledger_begin)
string(FIND "${exceptions}" "## Carried — CMake units over the 200-line ceiling" ledger_end)
if(ledger_begin EQUAL -1 OR ledger_end EQUAL -1 OR NOT ledger_end GREATER ledger_begin)
    message(FATAL_ERROR "test-file ledger section could not be isolated")
endif()
math(EXPR ledger_length "${ledger_end} - ${ledger_begin}")
string(SUBSTRING "${exceptions}" ${ledger_begin} ${ledger_length} ledger_section)
string(REGEX MATCHALL "\\| `[^`]+` \\| [0-9]+ \\|" ledger_rows "${ledger_section}")
set(recorded_ledger)
foreach(row IN LISTS ledger_rows)
    string(REGEX REPLACE "^\\| `([^`]+)` \\| ([0-9]+) \\|$" "\\1 \\2" row "${row}")
    list(APPEND recorded_ledger "${row}")
endforeach()
list(LENGTH measured_ledger measured_count)
list(LENGTH recorded_ledger recorded_count)
if(NOT measured_count EQUAL 29 OR NOT recorded_count EQUAL 29 OR NOT "${measured_ledger}" STREQUAL "${recorded_ledger}")
    message(FATAL_ERROR "test-file ledger differs: measured ${measured_count} rows, recorded ${recorded_count}")
endif()
run_checked(diff_output "git diff --check" "${git_exe}" -C "${source_dir}" diff --check)
message(STATUS "example contracts passed: 8 focused tests, complete CTest suite, and 33 executables")
