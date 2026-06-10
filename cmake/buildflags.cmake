set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# NUCLEUS_SANITIZER selects the instrumentation flavor for sanitizer builds:
# "address" pairs ASan with UBSan (they compose; TSan composes with neither),
# "thread" is the data-race validator behind the concurrent-load claim.
set(NUCLEUS_SANITIZER "address" CACHE STRING
    "Sanitizer flavor for NUCLEUS_BUILD_SANITIZER builds: address or thread")
if(NUCLEUS_BUILD_SANITIZER AND NOT MSVC)
    if(NUCLEUS_SANITIZER STREQUAL "thread")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Coverage instrumentation, gcc/clang only and off by default -- never in a
# normal build. Applied PER TARGET rather than globally so that fetched
# third-party dependencies (Catch2, pugixml) are never instrumented: their gcov
# data is noise the report filters out anyway, and their sources are not
# resolvable relative to the project root, which newer gcovr treats as a hard
# error. Call nucleus_enable_coverage(<target>) on every first-party target
# whose translation units should emit counters -- the libraries AND the test
# executables (test TUs instantiate header-inline library code that would
# otherwise lose its counts). MSVC has no gcov equivalent here, so the flags
# are guarded to the GNU/Clang front ends.
if(NUCLEUS_COVERAGE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(WARNING "NUCLEUS_COVERAGE is only supported on gcc/clang; ignoring.")
endif()

function(nucleus_enable_coverage target)
    if(NOT NUCLEUS_COVERAGE OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()
    target_compile_options(${target} PRIVATE
        --coverage -fprofile-arcs -ftest-coverage -O0 -g)
    # PUBLIC so every executable linking an instrumented library pulls the gcov
    # runtime into its own link; BUILD_INTERFACE so the option never leaks into
    # the installed export.
    target_link_options(${target} PUBLIC $<BUILD_INTERFACE:--coverage>)
endfunction()
