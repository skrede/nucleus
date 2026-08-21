# Ceiling gate for the units one change actually touches. Every measured unit is inside the
# line and function ceilings, or inside the figure EXCEPTIONS.md records for it -- a row in
# the register is the only sanctioned escape. No base revision is read, so a unit is held to
# the rule as written rather than to whatever an earlier commit happened to measure.
# C++ and CMake units are measured; anything else is skipped.
#
# Usage: cmake -DNUCLEUS_SIZE_FILES='a.h;b.cpp' -P tests/cmake/check_local_size_growth.cmake

cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/size_register.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/test_case_bodies.cmake)

set(file_ceiling 200)
set(function_ceiling 25)

# Defined but empty is the dangerous form: it measures nothing, exits 0, and leaves a log a
# reader cannot tell apart from a clean pass.
if(NOT DEFINED NUCLEUS_SIZE_FILES OR NUCLEUS_SIZE_FILES STREQUAL "")
    message(FATAL_ERROR "NUCLEUS_SIZE_FILES must name the sources to measure")
endif()

find_program(ctags_exe NAMES ctags)
find_program(jq_exe NAMES jq)
if(NOT ctags_exe OR NOT jq_exe)
    message(FATAL_ERROR "ctags and jq are required to measure function spans")
endif()

function(measure_lines path out)
    file(READ "${path}" content)
    string(REGEX MATCHALL "\n" newlines "${content}")
    list(LENGTH newlines count)
    set(${out} ${count} PARENT_SCOPE)
endfunction()

# Called for C++ units alone, so a zero here means the unit holds no function -- never that the
# tools failed. Mapping a tool failure onto zero passes every function check without having
# measured one, and both commands are checked because a ctags that dies still leaves jq to
# summarize its empty output successfully.
function(measure_functions path out_over out_longest)
    execute_process(
        COMMAND ${ctags_exe} --output-format=json --fields=+ne --languages=C++
                --c++-kinds=f -o - "${path}"
        COMMAND ${jq_exe} -s -r
                "[.[] | select(._type == \"tag\" and .kind == \"function\") | (.end - .line + 1)] | \"\\(map(select(. > ${function_ceiling})) | length) \\(max // 0)\""
        OUTPUT_VARIABLE measured OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE reported RESULTS_VARIABLE statuses)
    foreach(status IN LISTS statuses)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${path}: function spans could not be measured -- ${reported}")
        endif()
    endforeach()
    if(NOT measured MATCHES "^([0-9]+) ([0-9]+)$")
        message(FATAL_ERROR "${path}: function spans measured as '${measured}', which is not a pair of counts")
    endif()
    set(${out_over} ${CMAKE_MATCH_1} PARENT_SCOPE)
    set(${out_longest} ${CMAKE_MATCH_2} PARENT_SCOPE)
endfunction()

# Each metric is judged alone. Coupling them refuses a unit that crossed back under one
# ceiling because the other is still over, which is exactly the work a ceiling wants.
#
# The register is consulted even for a metric already inside its ceiling: a row whose unit
# has fallen back under the ceiling is a row to delete, and the passing path would never
# reach the lookup that notices. Drift is reported and never refused -- refusing a unit for
# measuring better than its row would refuse work that improves the tree.
#
# A refusal is worded verdict first so the words a caller greps for stay on the first line
# of the paragraph CMake wraps a fatal message into, however long the path trailing them is.
function(judge_metric unit metric measured ceiling registry out_refusal out_note)
    set(${out_refusal} "" PARENT_SCOPE)
    set(${out_note} "${metric} ${measured}" PARENT_SCOPE)
    registered_figure(${registry} "${unit}" figure)
    if(NOT figure STREQUAL "none" AND measured LESS figure)
        message(STATUS "${unit}: ${metric} ${measured} sits under its registered ${figure} -- tighten that row")
    endif()
    if(NOT measured GREATER ceiling)
        set(${out_note} "${metric} ${measured} within ${ceiling}" PARENT_SCOPE)
    elseif(figure STREQUAL "none")
        set(${out_refusal} "over the ${metric} ceiling of ${ceiling}: ${measured}, no register row -- ${unit}" PARENT_SCOPE)
    elseif(measured GREATER figure)
        set(${out_refusal} "exceeds the registered ${figure} on ${metric}: ${measured} -- ${unit}" PARENT_SCOPE)
    else()
        set(${out_note} "${metric} ${measured} within its registered ${figure}" PARENT_SCOPE)
    endif()
endfunction()

# The count column is held to as well as the span, which alone lets a registered unit multiply
# the functions it carries over the ceiling without limit as long as none grows past the longest
# one recorded -- and lets the drift line read that multiplication as an improvement.
function(judge_function_count unit measured out_refusal)
    set(${out_refusal} "" PARENT_SCOPE)
    registered_figure(size_register_function_counts "${unit}" allowed)
    if(allowed STREQUAL "none")
        return()
    endif()
    if(measured GREATER allowed)
        set(${out_refusal} "exceeds the registered ${allowed} over-ceiling functions: ${measured} -- ${unit}" PARENT_SCOPE)
    elseif(measured LESS allowed)
        message(STATUS "${unit}: ${measured} over-ceiling functions against a registered ${allowed} -- tighten that row")
    endif()
endfunction()

read_size_register()
set(refusals "")
set(measured_units 0)

foreach(path IN LISTS NUCLEUS_SIZE_FILES)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${path}: no such file to measure")
    endif()
    normalized_unit_path("${path}" unit)
    get_filename_component(name "${unit}" NAME)
    if(NOT unit MATCHES "\\.(h|hpp|cpp|cc|cmake)$" AND NOT name STREQUAL "CMakeLists.txt")
        message(STATUS "${unit}: not a C++ or CMake unit -- skipped")
        continue()
    endif()
    math(EXPR measured_units "${measured_units} + 1")
    measure_lines("${path}" lines)
    judge_metric("${unit}" "line count" ${lines} ${file_ceiling}
                 size_register_lines line_refusal line_note)
    set(span_refusal "")
    set(count_refusal "")
    set(body_refusal "")
    set(notes "${line_note}")
    if(unit MATCHES "\\.(h|hpp|cpp|cc)$")
        measure_functions("${path}" over_ceiling longest)
        judge_metric("${unit}" "function span" ${longest} ${function_ceiling}
                     size_register_functions span_refusal span_note)
        judge_function_count("${unit}" ${over_ceiling} count_refusal)
        string(APPEND notes ", ${span_note}")
        if(unit MATCHES "^tests/")
            measure_test_case_bodies("${path}" body_longest body_line)
            judge_test_case_bodies("${unit}" ${body_longest} ${body_line} body_refusal body_note)
            string(APPEND notes ", ${body_note}")
        endif()
    endif()
    foreach(refusal IN ITEMS "${line_refusal}" "${span_refusal}" "${count_refusal}" "${body_refusal}")
        if(NOT refusal STREQUAL "")
            list(APPEND refusals "${refusal}")
        endif()
    endforeach()
    if(line_refusal STREQUAL "" AND span_refusal STREQUAL ""
       AND count_refusal STREQUAL "" AND body_refusal STREQUAL "")
        message(STATUS "${unit}: ${notes}")
    endif()
endforeach()

if(NOT refusals STREQUAL "")
    list(LENGTH refusals refused)
    string(REPLACE ";" "\n" report "${refusals}")
    message(FATAL_ERROR "${refused} refused metric(s):\n${report}")
endif()

message(STATUS "measured ${measured_units} unit(s) against the ceilings; none refused")
