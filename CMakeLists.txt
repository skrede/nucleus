cmake_minimum_required(VERSION 3.24)
project(nucleus VERSION 0.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(NUCLEUS_BUILD_TESTS "Build tests" OFF)
option(NUCLEUS_BUILD_EXAMPLES "Build examples" OFF)
option(NUCLEUS_BUILD_SANITIZER "Build with AddressSanitizer" OFF)
option(NUCLEUS_COVERAGE "Instrument the library and tests for coverage (gcc/clang)" OFF)
option(NUCLEUS_FETCH_TEST_DEPS "Fetch test dependencies (Catch2) via FetchContent" ON)

include(cmake/buildflags.cmake)

# ---------------------------------------------------------------------------
# Diagnostic formatting backend
#
# std::format is the interface vocabulary, but libc++/Apple Clang historically
# lagged on it. Detect __cpp_lib_format; if the standard library lacks it, pull
# {fmt} (the reference implementation) so the format() seam stays buildable. The
# probe runs here, before add_subdirectory(lib), so its result and the recorded
# NUCLEUS_FMT_REQUIRED stay in this scope for the package config below; the fmt
# link itself is wired onto nucleus_core inside lib/CMakeLists.txt.
# ---------------------------------------------------------------------------
include(CheckCXXSourceCompiles)
# Drive the probe with the compiler's own standard selection rather than a
# hardcoded GNU flag: -std=c++20 is wrong on MSVC (which wants /std:c++20) and
# would make the probe falsely fail there, dragging in {fmt} despite MSVC having
# std::format. CMAKE_CXX_STANDARD makes try_compile pick the right per-compiler
# flag on every toolchain.
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
check_cxx_source_compiles(
    "#include <version>
     #ifndef __cpp_lib_format
     #error no std::format
     #endif
     int main() {}"
    NUCLEUS_HAVE_STD_FORMAT
)
unset(CMAKE_REQUIRED_FLAGS)

# Whether {fmt} is part of the link interface. When the fallback is active, fmt
# appears PUBLICly (the fallback lives in a public header), so the installed
# package config must find_dependency(fmt) for the exported reference to resolve
# downstream. Recorded here and consumed by the config template below.
set(NUCLEUS_FMT_REQUIRED FALSE)

if(NOT NUCLEUS_HAVE_STD_FORMAT)
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.2
        FIND_PACKAGE_ARGS NAMES fmt
    )
    FetchContent_MakeAvailable(fmt)
    set(NUCLEUS_FMT_REQUIRED TRUE)
endif()

# Whether the XML module (and thus its pugixml dependency) is built and installed.
# The module lives in lib/xml and flips this via PARENT_SCOPE when built; the
# package config consumes it to decide whether to find_dependency(pugixml).
option(NUCLEUS_BUILD_SOURCE_XML "Build the XML source module (fetches pugixml)" ON)
set(NUCLEUS_XML_INSTALLED FALSE)

# ---------------------------------------------------------------------------
# Per-target library tree
#
# lib/ stands up each module (core, then the format modules) behind one
# nucleus_add_module() helper. core's EXPORT_NAME is nucleus, so the installed
# export keeps defining nucleus::nucleus for existing consumers. The format
# modules (xml today; env/args/runtime next) link core but core cannot see them.
# ---------------------------------------------------------------------------
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
# The single export set; each module joined it via nucleus_add_module(). Generated
# once here so it sees every installed target (core + any format modules).
install(EXPORT nucleus-targets
    FILE nucleus-targets.cmake
    NAMESPACE nucleus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nucleus
)

# ---------------------------------------------------------------------------
# Package config
#
# Generate and install the find_package(nucleus) entry point. Without these a
# consumer's find_package(nucleus) fails even though the targets export exists.
# The config includes the targets export and, when the XML module is part of
# this install, requires pugixml so the exported $<LINK_ONLY:pugixml::pugixml>
# reference resolves for adopters. Emitted last so NUCLEUS_XML_INSTALLED is
# final.
# ---------------------------------------------------------------------------
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nucleus-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nucleus-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nucleus
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nucleus-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/nucleus-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nucleus-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nucleus
)

if(NUCLEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NUCLEUS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
