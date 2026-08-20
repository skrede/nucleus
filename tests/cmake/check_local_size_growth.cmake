# Ceiling gate for the units one change actually touches. A source that is new, or
# that already meets the ceilings at the base revision, must meet them now; a source
# that is already over a ceiling there must not grow past that measurement.
# C++ and CMake units are measured; anything else is skipped.
#
# Usage: cmake -DNUCLEUS_SIZE_FILES='a.h;b.cpp' -P tests/cmake/check_local_size_growth.cmake
#
# NUCLEUS_SIZE_BASE_REF names the revision each unit is compared against and defaults
# to HEAD, which pits staged or working-tree content against the last commit. A caller
# whose working tree already equals HEAD -- a pull-request check on a merged result --
# must pass a merge base instead, or every no-growth comparison is against an identical
# copy and can never fail.

cmake_minimum_required(VERSION 3.20)

set(file_ceiling 200)
set(function_ceiling 25)

if(NOT DEFINED NUCLEUS_SIZE_FILES)
    message(FATAL_ERROR "NUCLEUS_SIZE_FILES must name the sources to measure")
endif()

if(NOT DEFINED NUCLEUS_SIZE_BASE_REF)
    set(NUCLEUS_SIZE_BASE_REF HEAD)
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

# The same source as recorded at the base revision, staged into a scratch copy that keeps
# the original suffix so ctags still recognizes the language. Empty when the source is new.
function(base_revision_of path out)
    get_filename_component(name "${path}" NAME)
    set(scratch "${CMAKE_CURRENT_BINARY_DIR}/size-gate-base-${name}")
    execute_process(COMMAND git show "${NUCLEUS_SIZE_BASE_REF}:${path}"
                    OUTPUT_FILE "${scratch}" ERROR_QUIET RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        file(REMOVE "${scratch}")
        set(scratch "")
    endif()
    set(${out} "${scratch}" PARENT_SCOPE)
endfunction()

function(enforce_ceilings path lines longest)
    if(lines GREATER file_ceiling)
        message(FATAL_ERROR "${path}: ${lines} lines exceeds the ${file_ceiling}-line ceiling")
    endif()
    if(longest GREATER function_ceiling)
        message(FATAL_ERROR "${path}: longest function is ${longest} lines, over the ${function_ceiling}-line ceiling")
    endif()
    message(STATUS "${path}: ${lines} lines, longest function ${longest} -- within ceilings")
endfunction()

function(enforce_no_growth path lines longest base_lines base_longest)
    if(lines GREATER base_lines)
        message(FATAL_ERROR "${path}: grew from ${base_lines} to ${lines} lines while already over a ceiling")
    endif()
    if(longest GREATER base_longest)
        message(FATAL_ERROR "${path}: longest function grew from ${base_longest} to ${longest} lines")
    endif()
    message(STATUS "${path}: ${lines} lines (was ${base_lines}), longest function ${longest} (was ${base_longest}) -- no growth")
endfunction()

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
    base_revision_of("${path}" base_copy)
    if(base_copy STREQUAL "")
        enforce_ceilings("${path}" ${lines} ${longest})
        continue()
    endif()
    measure_lines("${base_copy}" base_lines)
    measure_longest_function("${base_copy}" base_longest)
    file(REMOVE "${base_copy}")
    if(base_lines GREATER file_ceiling OR base_longest GREATER function_ceiling)
        enforce_no_growth("${path}" ${lines} ${longest} ${base_lines} ${base_longest})
    else()
        enforce_ceilings("${path}" ${lines} ${longest})
    endif()
endforeach()
