# Catch2 introduces a test case through a macro, so ctags emits no function tag for it and the
# function ceiling never saw the bodies that hold nearly all test logic. They are measured here
# instead, under a rule that applies to the test tree alone: the 25-line function ceiling is a
# guideline for a test-case body rather than a gate, and a body past the ceiling below is refused
# outright, with no register row available to excuse it.

include_guard(GLOBAL)

set(test_case_body_ceiling 60)

# The file is walked as lines rather than read with file(STRINGS), which drops the blank lines a
# span measured between two line numbers has to count. Literals and comments are removed first so
# a brace in test data cannot move the depth count; brackets and semicolons go with them because
# CMake will not split a value holding an unmatched bracket, and C++ subscripts produce them
# constantly. Neither removal can affect a brace count or a macro line.
function(test_case_body_lines path out)
    file(READ "${path}" content)
    string(REGEX REPLACE "\r" "" content "${content}")
    string(REGEX REPLACE "\\\\[^\n]" "" content "${content}")
    string(REGEX REPLACE "\"[^\"\n]*\"" "" content "${content}")
    string(REGEX REPLACE "'[^'\n]*'" "" content "${content}")
    string(REGEX REPLACE "//[^\n]*" "" content "${content}")
    string(REGEX REPLACE "[][\\\\;]" "" content "${content}")
    string(REGEX REPLACE "\n" ";" lines "${content}")
    set(${out} "${lines}" PARENT_SCOPE)
endfunction()

function(count_character text character out)
    string(REGEX REPLACE "[^${character}]" "" only "${text}")
    string(LENGTH "${only}" count)
    set(${out} ${count} PARENT_SCOPE)
endfunction()

function(test_case_body_opens line out)
    set(${out} OFF PARENT_SCOPE)
    if(line MATCHES "^[ \t]*(TEST_CASE|SCENARIO|TEMPLATE_TEST_CASE|TEST_CASE_METHOD)[ \t]*\\(")
        set(${out} ON PARENT_SCOPE)
    endif()
endfunction()

# A body ends at the closing brace the project's formatting puts in column one, but only where
# the running depth agrees that the brace is the body's own. A brace the removals above did not
# reach -- one inside a raw string spanning several lines, say -- therefore cannot end a body
# early and quietly measure it short; it is passed over, and a body that never balances is a
# hard failure rather than a pass.
function(measure_body_end path lines start out)
    set(depth 0)
    set(number 0)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        if(number LESS start)
            continue()
        endif()
        count_character("${line}" "{" opened)
        count_character("${line}" "}" closed)
        math(EXPR depth "${depth} + ${opened} - ${closed}")
        if(depth EQUAL 0 AND line MATCHES "^\\}")
            set(${out} ${number} PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "${path}:${start}: the test-case body never closes on a brace in column one")
endfunction()

function(measure_test_case_bodies path out_longest out_line)
    test_case_body_lines("${path}" lines)
    set(number 0)
    set(longest 0)
    set(worst 0)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        test_case_body_opens("${line}" opens)
        if(NOT opens)
            continue()
        endif()
        measure_body_end("${path}" "${lines}" ${number} end)
        math(EXPR span "${end} - ${number} + 1")
        if(span GREATER longest)
            set(longest ${span})
            set(worst ${number})
        endif()
    endforeach()
    set(${out_longest} ${longest} PARENT_SCOPE)
    set(${out_line} ${worst} PARENT_SCOPE)
endfunction()

function(judge_test_case_bodies unit longest line out_refusal out_note)
    set(${out_refusal} "" PARENT_SCOPE)
    set(${out_note} "test-case body ${longest} within ${test_case_body_ceiling}" PARENT_SCOPE)
    if(longest GREATER test_case_body_ceiling)
        set(${out_refusal}
            "over the test-case body ceiling of ${test_case_body_ceiling}: ${longest}, which no register row may excuse -- ${unit}:${line}"
            PARENT_SCOPE)
    endif()
endfunction()
