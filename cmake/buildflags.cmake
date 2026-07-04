set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emitted for every configure so tooling (clang-tidy, clangd) has a compilation
# database without a bespoke build step.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
    message(STATUS "nucleus: CMAKE_BUILD_TYPE not set -- defaulting to Debug "
                   "(pass -DCMAKE_BUILD_TYPE=Release for an optimized build)")
endif()

# The single gate for treating warnings as errors on first-party targets.
# Default ON: the first-party surface is clean under the curated warning set, so
# the gate is enforced by default for every local and CI build. This is the ONLY
# switch that injects -Werror / /WX -- no per-job or per-target duplication --
# so a warning cannot be fatal under one compiler while silently green under
# another. Pass -DNUCLEUS_WERROR=OFF to demote warnings to non-fatal locally.
option(NUCLEUS_WERROR "Treat warnings as errors for first-party targets" ON)

# Warnings are applied PER TARGET, never directory-globally: a directory-scope
# add_compile_options at the root would leak /W4 / -Wpedantic (and on MSVC
# /permissive-) into the FetchContent builds of fmt, pugixml, and Catch2, whose
# code is not ours to lint. Call nucleus_warnings(<target>) on every first-party
# compiled target.
function(nucleus_warnings target)
    if(MSVC)
        # /utf-8: MSVC otherwise reads source as the active code page, silently
        # double-encoding any non-ASCII byte in a u8 literal.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(NUCLEUS_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # Maxed GCC set. The first block is portable (clang accepts every
            # flag too, but it lives on the GCC branch to keep clang on its own
            # -Weverything superset below). The second block is GCC-only: these
            # flags are unknown to clang, so under -Werror an unguarded clang
            # build would hard-error on -Wunknown-warning-option.
            target_compile_options(${target} PRIVATE
                -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
                -Wcast-qual -Wnon-virtual-dtor -Woverloaded-virtual
                -Wnull-dereference -Wdouble-promotion -Wformat=2
                -Wimplicit-fallthrough -Wsuggest-override -Wextra-semi
                -Wzero-as-null-pointer-constant
                -Wuseless-cast -Wlogical-op -Wduplicated-cond
                -Wduplicated-branches -Wmisleading-indentation)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")  # AppleClang + upstream Clang
            # Curated -Weverything: opt in to the whole clang diagnostic
            # universe, then subtract the categories that are pure noise for a
            # value-semantics C++20 library. This -Wno- block is the single
            # enumerated, justified opt-out list for the whole project. Some
            # entries are inert on Apple clang 16 but flood on Linux clang-18,
            # so the list is kept complete and portable rather than trimmed to
            # what fires locally.
            target_compile_options(${target} PRIVATE
                -Weverything
                # This is a C++20 library; the compat families warn that modern
                # constructs are "incompatible with C++98/C++20".
                -Wno-c++98-compat
                -Wno-c++98-compat-pedantic
                -Wno-c++98-compat-extra-semi
                -Wno-c++20-compat
                # Reports every struct with padding; meaningless for value types
                # and would force artificial member reordering.
                -Wno-padded
                # Fires on polymorphic classes whose vtable cannot be pinned to
                # one TU; a known false-positive magnet for header-heavy code.
                -Wno-weak-vtables
                # Intentional Meyers-style function-local sentinels (empty
                # string/vector, cached type_info) legitimately have exit-time
                # destructors / non-trivial construction; the pattern is by
                # design, not a defect.
                -Wno-exit-time-destructors
                -Wno-global-constructors
                # nucleus uses class-template-argument deduction idiomatically
                # for types without explicit deduction guides.
                -Wno-ctad-maybe-unsupported
                # Keep -Wswitch (all enumerators without a default); drop the
                # variants that demand an explicit case per enumerator or a
                # mandatory default -- the latter conflicts with the
                # covered-switch-default fixes that remove dead defaults.
                -Wno-switch-enum
                -Wno-switch-default
                # Host artifact: warns that Homebrew include roots are "unsafe
                # for cross-compilation"; irrelevant to a native build.
                -Wno-poison-system-directories
                # Experimental -fbounds-safety hardening family; extremely noisy
                # and aimed at adopters of that model, not general libraries.
                -Wno-unsafe-buffer-usage
                -Wno-unsafe-buffer-usage-in-container)
        endif()

        if(NUCLEUS_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

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
