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
)

set(NUCLEUS_SOURCES
    src/nucleus/version.cpp
)

add_library(nucleus ${NUCLEUS_SOURCES})
add_library(nucleus::nucleus ALIAS nucleus)

target_compile_features(nucleus PUBLIC cxx_std_20)

target_include_directories(nucleus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(nucleus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
