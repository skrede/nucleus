# Ceiling gate for the units one change actually touches. Every measured unit is inside the
# line and function ceilings, or inside the figure EXCEPTIONS.md records for it -- a row in
# the register is the only sanctioned escape. No base revision is read, so a unit is held to
# the rule as written rather than to whatever an earlier commit happened to measure.
# C++ and CMake units are measured; anything else is skipped.
#
# Usage: cmake -DNUCLEUS_SIZE_FILES='a.h;b.cpp' -P tests/cmake/check_local_size_growth.cmake

cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/size_register.cmake)

set(file_ceiling 200)
set(function_ceiling 25)

if(NOT DEFINED NUCLEUS_SIZE_FILES)
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

# The longest function span in a C++ unit, or 0 for a unit ctags does not parse.
function(measure_longest_function path out)
    execute_process(
        COMMAND ${ctags_exe} --output-format=json --fields=+ne --languages=C++
                --c++-kinds=f -o - "${path}"
        COMMAND ${jq_exe} -s -r
                "([.[] | select(._type == \"tag\" and .kind == \"function\") | (.end - .line + 1)] | max) // 0"
        OUTPUT_VARIABLE longest OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE ignored RESULT_VARIABLE status)
    if(NOT status EQUAL 0 OR NOT longest MATCHES "^[0-9]+$")
        set(longest 0)
    endif()
    set(${out} ${longest} PARENT_SCOPE)
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
function(judge_metric path metric measured ceiling registry out_refusal out_note)
    set(${out_refusal} "" PARENT_SCOPE)
    set(${out_note} "${metric} ${measured}" PARENT_SCOPE)
    registered_figure(${registry} "${path}" figure)
    if(NOT figure STREQUAL "none" AND measured LESS figure)
        message(STATUS "${path}: ${metric} ${measured} sits under its registered ${figure} -- tighten that row")
    endif()
    if(NOT measured GREATER ceiling)
        set(${out_note} "${metric} ${measured} within ${ceiling}" PARENT_SCOPE)
    elseif(figure STREQUAL "none")
        set(${out_refusal} "over the ${metric} ceiling of ${ceiling}: ${measured}, no register row -- ${path}" PARENT_SCOPE)
    elseif(measured GREATER figure)
        set(${out_refusal} "exceeds the registered ${figure} on ${metric}: ${measured} -- ${path}" PARENT_SCOPE)
    else()
        set(${out_note} "${metric} ${measured} within its registered ${figure}" PARENT_SCOPE)
    endif()
endfunction()

read_size_register()
set(refusals "")

foreach(path IN LISTS NUCLEUS_SIZE_FILES)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${path}: no such file to measure")
    endif()
    get_filename_component(name "${path}" NAME)
    if(NOT path MATCHES "\\.(h|hpp|cpp|cc|cmake)$" AND NOT name STREQUAL "CMakeLists.txt")
        message(STATUS "${path}: not a C++ or CMake unit -- skipped")
        continue()
    endif()
    measure_lines("${path}" lines)
    measure_longest_function("${path}" longest)
    judge_metric("${path}" "line count" ${lines} ${file_ceiling}
                 size_register_lines line_refusal line_note)
    judge_metric("${path}" "function span" ${longest} ${function_ceiling}
                 size_register_functions span_refusal span_note)
    if(line_refusal STREQUAL "" AND span_refusal STREQUAL "")
        message(STATUS "${path}: ${line_note}, ${span_note}")
    endif()
    if(NOT line_refusal STREQUAL "")
        list(APPEND refusals "${line_refusal}")
    endif()
    if(NOT span_refusal STREQUAL "")
        list(APPEND refusals "${span_refusal}")
    endif()
endforeach()

if(NOT refusals STREQUAL "")
    list(LENGTH refusals refused)
    string(REPLACE ";" "\n" report "${refusals}")
    message(FATAL_ERROR "${refused} refused metric(s):\n${report}")
endif()
