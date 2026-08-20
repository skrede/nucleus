# Runs the ordinal probe at an actual 32-bit width: directly on a natively 32-bit
# host, through a -m32 compile on GNU/Clang, and through a Win32-generator subbuild
# on MSVC. Where none of those is available the gate reports rather than guesses --
# skip code 77 in normal mode, a hard failure under REQUIRE_32BIT -- and prints the
# attempted command, the compiler's own diagnostic, and host-specific remediation.
#
# Usage: cmake -DPROBE_EXE=... -DPROBE_SOURCE=... -P tests/cmake/run_ordinal_width.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED REQUIRE_32BIT)
    set(REQUIRE_32BIT OFF)
endif()
if(NOT DEFINED WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(NOT DEFINED REMEDIATION)
    if(CMAKE_HOST_WIN32)
        set(REMEDIATION "install the x86 MSVC build tools so the Win32 generator resolves")
    elseif(CMAKE_HOST_APPLE)
        set(REMEDIATION "macOS carries no 32-bit runtime; run this gate on a Linux or Windows host")
    else()
        set(REMEDIATION "install the 32-bit toolchain (g++-multilib and a 32-bit libstdc++)")
    endif()
endif()

# cmake_language(EXIT) is 3.29; below it a script can only leave with 0 or 1, so the
# probe's own --unsupported command carries these statuses instead.
function(ordinal_exit code)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${code})
    endif()
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "ordinal-width: exit ${code} needs CMake 3.29 or newer")
    endif()
endfunction()

function(report_unavailable attempted diagnostic)
    message(STATUS "ordinal-width: 32-bit verification unavailable")
    message(STATUS "ordinal-width: attempted command: ${attempted}")
    message(STATUS "ordinal-width: compiler diagnostic: ${diagnostic}")
    message(STATUS "ordinal-width: remediation: ${REMEDIATION}")
    if(REQUIRE_32BIT)
        message(FATAL_ERROR "ordinal-width: a 32-bit run is required here and is unavailable")
    endif()
    ordinal_exit(77)
endfunction()

function(run_probe_binary binary)
    execute_process(COMMAND "${binary}" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "ordinal-width: the 32-bit probe reported ${status}")
    endif()
    ordinal_exit(0)
endfunction()

# One throwaway Win32-generator project, which is how MSVC selects a 32-bit target.
function(build_win32_probe out)
    set(tree "${WORK_DIR}/ordinal-width-win32")
    file(WRITE "${tree}/CMakeLists.txt"
         "cmake_minimum_required(VERSION 3.24)\n"
         "project(ordinal_width_win32 CXX)\n"
         "add_executable(ordinal_width_probe_win32 \"${PROBE_SOURCE}\")\n"
         "target_compile_features(ordinal_width_probe_win32 PRIVATE cxx_std_20)\n"
         "target_include_directories(ordinal_width_probe_win32 PRIVATE \"${INCLUDE_DIRS}\")\n")
    set(${out} "${tree}" PARENT_SCOPE)
endfunction()

if(SIMULATE_UNAVAILABLE)
    report_unavailable("${ATTEMPTED_COMMAND}" "${COMPILER_DIAGNOSTIC}")
endif()

if(NATIVE_POINTER_SIZE EQUAL 4)
    run_probe_binary("${PROBE_EXE}")
endif()

if(CXX_COMPILER_ID STREQUAL "MSVC")
    build_win32_probe(tree)
    set(attempted "${CMAKE_COMMAND} -S ${tree} -B ${tree}/build -A Win32")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${tree}" -B "${tree}/build" -A Win32
        RESULT_VARIABLE status ERROR_VARIABLE diagnostic OUTPUT_QUIET)
    if(status EQUAL 0)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${tree}/build" --config Release
            RESULT_VARIABLE status ERROR_VARIABLE diagnostic OUTPUT_QUIET)
    endif()
    if(NOT status EQUAL 0)
        report_unavailable("${attempted}" "${diagnostic}")
    endif()
    file(GLOB built "${tree}/build/*/ordinal_width_probe_win32.exe")
    list(GET built 0 binary)
    run_probe_binary("${binary}")
endif()

if(NOT CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    report_unavailable("no 32-bit strategy for compiler '${CXX_COMPILER_ID}'"
                       "the gate knows -m32 for GNU/Clang and the Win32 generator for MSVC")
endif()

set(binary "${WORK_DIR}/ordinal_width_probe_m32")
set(command "${CXX_COMPILER}" -m32 -std=c++20 "-I${INCLUDE_DIRS}"
            "${PROBE_SOURCE}" -o "${binary}")
string(JOIN " " attempted ${command})
execute_process(COMMAND ${command}
                RESULT_VARIABLE status ERROR_VARIABLE diagnostic OUTPUT_QUIET)
if(NOT status EQUAL 0)
    report_unavailable("${attempted}" "${diagnostic}")
endif()
run_probe_binary("${binary}")
