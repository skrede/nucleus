include_guard(GLOBAL)

set(nucleus_format_package clang-format-18)
string(REGEX REPLACE "^.*-" "" nucleus_format_major "${nucleus_format_package}")

function(format_tool_version exe out_major)
    set(${out_major} "" PARENT_SCOPE)
    execute_process(COMMAND "${exe}" --version
        RESULT_VARIABLE status OUTPUT_VARIABLE reported ERROR_QUIET)
    if(status EQUAL 0 AND reported MATCHES "([0-9]+)\\.[0-9]+\\.[0-9]+")
        set(${out_major} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

# Resolution picks between two modes instead of simply refusing, because the gate that calls it
# also configures, builds, runs the complete test suite, executes every example binary, asserts
# their output contracts, runs the size gate and verifies the ledger. Refusing on the formatter's
# version would take all of that away from a contributor whose distribution ships a different one,
# rather than only the format step.
#
# Which mode applies is decided fail-safe: any run under CI is authoritative unless it says
# otherwise, rather than advisory unless a caller remembers to arm it. Arming used to depend on a
# single -D on a single workflow step, so deleting that one line downgraded every later run to a
# warning and a passing check with nothing anywhere failing. An omission must not be able to make
# this quieter -- it can only make it louder.
function(gate_is_authoritative out)
    if(NUCLEUS_GATE_AUTHORITATIVE OR (NOT "$ENV{CI}" STREQUAL "" AND NOT NUCLEUS_GATE_ADVISORY))
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(resolve_pinned_formatter out_exe out_status)
    find_program(nucleus_format_exe NAMES ${nucleus_format_package} clang-format)
    set(found "")
    if(nucleus_format_exe)
        format_tool_version("${nucleus_format_exe}" found)
        set(detail "${nucleus_format_exe} reports major version ${found}")
    else()
        set(detail "no formatter was found")
    endif()
    if(found STREQUAL "${nucleus_format_major}")
        set(${out_exe} "${nucleus_format_exe}" PARENT_SCOPE)
        set(${out_status} "" PARENT_SCOPE)
        return()
    endif()
    set(reason "the format check needs major version ${nucleus_format_major}, and ${detail}; install ${nucleus_format_package}")
    gate_is_authoritative(authoritative)
    if(authoritative)
        message(FATAL_ERROR "${reason}")
    endif()
    message(WARNING "${reason}\nevery other step of this gate still runs; the format step is not verified")
    set(${out_exe} "" PARENT_SCOPE)
    set(${out_status} "${reason}" PARENT_SCOPE)
endfunction()

# An empty list would make clang-format exit 0 having read nothing, which is the shape
# check_local_size_growth.cmake documents as the dangerous one: a step that reports success
# because it was given no work.
function(run_format_check exe files_var)
    list(LENGTH ${files_var} count)
    if(count EQUAL 0)
        message(FATAL_ERROR "the format check was given no files")
    endif()
    execute_process(COMMAND "${exe}" --dry-run --Werror ${${files_var}}
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "clang-format failed with status ${status}\n${stderr}")
    endif()
endfunction()
