cmake_minimum_required(VERSION 3.28)
project(nucleus VERSION 0.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(NUCLEUS_BUILD_TESTS "Build tests" OFF)
option(NUCLEUS_BUILD_EXAMPLES "Build examples" OFF)
option(NUCLEUS_BUILD_SANITIZER "Build with AddressSanitizer" OFF)
option(NUCLEUS_FETCH_TEST_DEPS "Fetch test dependencies (Catch2) via FetchContent" ON)

include(cmake/buildflags.cmake)

# ---------------------------------------------------------------------------
# nucleus core library
#
# The public API is intentionally NOT defined here yet -- it is produced by the
# project roadmap, not the scaffold. Only a minimal version walking-skeleton
# exists so the build, install/export, and test wiring are real and verifiable.
# ---------------------------------------------------------------------------
set(NUCLEUS_PUBLIC_HEADERS
    include/nucleus/version.h
    include/nucleus/result.h
    include/nucleus/format.h
    include/nucleus/nucleus.h
    include/nucleus/identity.h
    include/nucleus/log_sink.h
    include/nucleus/registration_policy.h
)

set(NUCLEUS_SOURCES
    src/nucleus/version.cpp
    src/nucleus/entry/nucleus.cpp
)

add_library(nucleus ${NUCLEUS_SOURCES})
add_library(nucleus::nucleus ALIAS nucleus)

target_compile_features(nucleus PUBLIC cxx_std_20)

target_include_directories(nucleus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(nucleus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Diagnostic formatting backend
#
# std::format is the interface vocabulary, but libc++/Apple Clang historically
# lagged on it. Detect __cpp_lib_format; if the standard library lacks it, pull
# {fmt} (the reference implementation) so the format() seam stays buildable. The
# dependency is PUBLIC because the fallback appears in a public header.
# ---------------------------------------------------------------------------
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
check_cxx_source_compiles(
    "#include <version>
     #ifndef __cpp_lib_format
     #error no std::format
     #endif
     int main() {}"
    NUCLEUS_HAVE_STD_FORMAT
)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT NUCLEUS_HAVE_STD_FORMAT)
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.2
        FIND_PACKAGE_ARGS NAMES fmt
    )
    FetchContent_MakeAvailable(fmt)
    target_link_libraries(nucleus PUBLIC fmt::fmt)
endif()

set_target_properties(nucleus PROPERTIES
    PUBLIC_HEADER "${NUCLEUS_PUBLIC_HEADERS}"
    VERSION ${PROJECT_VERSION}
)

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
install(TARGETS nucleus
    EXPORT nucleus-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus
)
install(EXPORT nucleus-targets
    FILE nucleus-targets.cmake
    NAMESPACE nucleus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nucleus
)

if(NUCLEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NUCLEUS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
