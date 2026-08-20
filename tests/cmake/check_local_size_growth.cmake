# Ceiling gate for the units one change actually touches. A source that is new, or
# that already meets the ceilings at HEAD, must meet them now; a source that is
# already over a ceiling at HEAD must not grow past its own HEAD measurement.
# C++ and CMake units are measured; anything else is skipped.
#
# Usage: cmake -DNUCLEUS_SIZE_FILES='a.h;b.cpp' -P tests/cmake/check_local_size_growth.cmake

cmake_minimum_required(VERSION 3.20)

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

# The same source as recorded at HEAD, staged into a scratch copy that keeps the
# original suffix so ctags still recognizes the language. Empty when the source is new.
function(head_revision_of path out)
    get_filename_component(name "${path}" NAME)
    set(scratch "${CMAKE_CURRENT_BINARY_DIR}/size-gate-head-${name}")
    execute_process(COMMAND git show "HEAD:${path}"
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

function(enforce_no_growth path lines longest head_lines head_longest)
    if(lines GREATER head_lines)
        message(FATAL_ERROR "${path}: grew from ${head_lines} to ${lines} lines while already over a ceiling")
    endif()
    if(longest GREATER head_longest)
        message(FATAL_ERROR "${path}: longest function grew from ${head_longest} to ${longest} lines")
    endif()
    message(STATUS "${path}: ${lines} lines (was ${head_lines}), longest function ${longest} (was ${head_longest}) -- no growth")
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
    head_revision_of("${path}" head_copy)
    if(head_copy STREQUAL "")
        enforce_ceilings("${path}" ${lines} ${longest})
        continue()
    endif()
    measure_lines("${head_copy}" head_lines)
    measure_longest_function("${head_copy}" head_longest)
    file(REMOVE "${head_copy}")
    if(head_lines GREATER file_ceiling OR head_longest GREATER function_ceiling)
        enforce_no_growth("${path}" ${lines} ${longest} ${head_lines} ${head_longest})
    else()
        enforce_ceilings("${path}" ${lines} ${longest})
    endif()
endforeach()
