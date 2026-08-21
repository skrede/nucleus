# The register of sanctioned and carried over-ceiling units, read as data. A unit over a
# ceiling passes the gate only when this file records a figure it does not exceed, so the
# lookup has to resolve the register from this unit's own location rather than from the
# caller's working directory.

include_guard(GLOBAL)

get_filename_component(size_register_root "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
set(size_register_file "${size_register_root}/EXCEPTIONS.md")

if(NOT EXISTS "${size_register_file}")
    message(FATAL_ERROR "size register missing: ${size_register_file}")
endif()

# Register rows name a unit relative to the repository root, so a caller listing the same unit
# absolutely or as ./path would otherwise be told it has no row -- a false refusal whose natural
# remedy is a duplicate row, turning a fail-closed bug into a permissive one.
function(normalized_unit_path path out)
    get_filename_component(absolute "${path}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(RELATIVE_PATH relative "${size_register_root}" "${absolute}")
    set(${out} "${relative}" PARENT_SCOPE)
endfunction()

function(size_register_cells row out)
    set(${out} "" PARENT_SCOPE)
    string(STRIP "${row}" row)
    if(NOT row MATCHES "^\\|.*\\|$")
        return()
    endif()
    string(REGEX REPLACE "^\\||\\|$" "" row "${row}")
    string(REPLACE "|" ";" cells "${row}")
    set(stripped "")
    foreach(cell IN LISTS cells)
        string(STRIP "${cell}" cell)
        list(APPEND stripped "${cell}")
    endforeach()
    set(${out} "${stripped}" PARENT_SCOPE)
endfunction()

function(size_register_path cell out)
    set(${out} "" PARENT_SCOPE)
    if(NOT cell MATCHES "^`(.+)`$")
        return()
    endif()
    set(path "${CMAKE_MATCH_1}")
    if(NOT path MATCHES "/")
        return()
    endif()
    get_filename_component(name "${path}" NAME)
    if(path MATCHES "\\.(h|hpp|cpp|cc|cmake)$" OR name STREQUAL "CMakeLists.txt")
        set(${out} "${path}" PARENT_SCOPE)
    endif()
endfunction()

# A row is a measurement because of its shape, never because of the section it sits in:
# the first cell must be a backtick-quoted path and the figures must be bare integers.
# Section walking would break the moment a table is added, reordered or retitled, and a
# looser shape test would let a prose row with two numbers in it fabricate a ceiling.
# A three-cell row records a count and a longest span; both are enforced.
function(size_register_entry row out_metric out_entry out_count)
    set(${out_metric} "" PARENT_SCOPE)
    size_register_cells("${row}" cells)
    list(LENGTH cells count)
    if(count LESS 2 OR count GREATER 3)
        return()
    endif()
    list(GET cells 0 first)
    list(GET cells 1 second)
    size_register_path("${first}" path)
    if(path STREQUAL "" OR NOT second MATCHES "^[0-9]+$")
        return()
    endif()
    if(count EQUAL 2)
        set(${out_metric} lines PARENT_SCOPE)
        set(${out_entry} "${path}=${second}" PARENT_SCOPE)
        return()
    endif()
    list(GET cells 2 third)
    if(third MATCHES "^[0-9]+$")
        set(${out_metric} functions PARENT_SCOPE)
        set(${out_entry} "${path}=${third}" PARENT_SCOPE)
        set(${out_count} "${path}=${second}" PARENT_SCOPE)
    endif()
endfunction()

# The register is split by hand rather than read with file(STRINGS) because CMake will not split
# a value holding an unmatched bracket, and the recipes quoted here are full of them: whole runs
# of rows would arrive glued into one, taking the fence markers below with them. Brackets,
# backslashes and semicolons are dropped first; no row's shape depends on any of the three.
function(register_rows out)
    file(READ "${size_register_file}" content)
    string(REGEX REPLACE "\r" "" content "${content}")
    string(REGEX REPLACE "[][\\\\;]" "" content "${content}")
    string(REGEX REPLACE "\n" ";" rows "${content}")
    set(${out} "${rows}" PARENT_SCOPE)
endfunction()

# Rows inside a fenced code block are documentation, not measurements. Reading them live means
# the most natural edit anyone makes to this file -- showing an example of what a row looks like
# -- silently grants a permanent exception to whatever unit the example names.
function(unfenced_rows out)
    register_rows(rows)
    set(kept "")
    set(fenced OFF)
    foreach(row IN LISTS rows)
        if(row MATCHES "^[ \t]*(```|~~~)")
            if(fenced)
                set(fenced OFF)
            else()
                set(fenced ON)
            endif()
        elseif(NOT fenced)
            list(APPEND kept "${row}")
        endif()
    endforeach()
    set(${out} "${kept}" PARENT_SCOPE)
endfunction()

function(read_size_register)
    unfenced_rows(rows)
    set(lines "")
    set(functions "")
    set(counts "")
    foreach(row IN LISTS rows)
        size_register_entry("${row}" metric entry count)
        if(metric STREQUAL "lines")
            list(APPEND lines "${entry}")
        elseif(metric STREQUAL "functions")
            list(APPEND functions "${entry}")
            list(APPEND counts "${count}")
        endif()
    endforeach()
    set(size_register_lines "${lines}" PARENT_SCOPE)
    set(size_register_functions "${functions}" PARENT_SCOPE)
    set(size_register_function_counts "${counts}" PARENT_SCOPE)
endfunction()

# Two rows for one unit and metric make the register ambiguous about what the unit is held to,
# and the looser of the two would win by position alone.
function(registered_figure list_name path out)
    set(found "")
    foreach(entry IN LISTS ${list_name})
        string(REGEX REPLACE "=[0-9]+$" "" entry_path "${entry}")
        if(entry_path STREQUAL path)
            string(REGEX REPLACE "^.*=" "" figure "${entry}")
            if(NOT found STREQUAL "")
                message(FATAL_ERROR
                        "the register names ${path} twice for one metric, at ${found} and ${figure}")
            endif()
            set(found "${figure}")
        endif()
    endforeach()
    if(found STREQUAL "")
        set(found none)
    endif()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()
