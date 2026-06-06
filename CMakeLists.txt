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
    include/nucleus/capability.h
    include/nucleus/registration_policy.h
)

# Public source-seam and keyspace headers. These are header-only contracts; they
# install under their subdirectory so the include paths stay stable for adopters.
set(NUCLEUS_SOURCE_HEADERS
    include/nucleus/source/source.h
    include/nucleus/source/parser.h
    include/nucleus/source/feature_gate.h
    include/nucleus/source/parser_adapter.h
    include/nucleus/source/document_source.h
)

set(NUCLEUS_SOURCE_ENV_HEADERS
    include/nucleus/source/env/env_source.h
)

set(NUCLEUS_KEYSPACE_HEADERS
    include/nucleus/keyspace/value.h
    include/nucleus/keyspace/entry.h
    include/nucleus/keyspace/key_path.h
    include/nucleus/keyspace/keyspace.h
)

set(NUCLEUS_SCHEMA_HEADERS
    include/nucleus/schema/anchor.h
    include/nucleus/schema/schema.h
)

set(NUCLEUS_SOURCE_ARGV_HEADERS
    include/nucleus/source/argv/cli_surface.h
    include/nucleus/source/argv/argv_source.h
)

set(NUCLEUS_SOURCES
    src/nucleus/version.cpp
    src/nucleus/entry/nucleus.cpp
    src/nucleus/tokenizer/tokenizer.cpp
    src/nucleus/tokenizer/token_lexer.cpp
    src/nucleus/tokenizer/scope_keys.cpp
    src/nucleus/tokenizer/resolver_scope.cpp
    src/nucleus/tokenizer/uuid.cpp
    src/nucleus/tokenizer/builtin_tokenizers.cpp
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

# Subdirectory headers preserve their relative path under the include root
# (PUBLIC_HEADER flattens, which would collide source/ and keyspace/ names).
install(FILES ${NUCLEUS_SOURCE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/source)
install(FILES ${NUCLEUS_SOURCE_ENV_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/source/env)
install(FILES ${NUCLEUS_KEYSPACE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/keyspace)
install(FILES ${NUCLEUS_SCHEMA_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/schema)
install(FILES ${NUCLEUS_SOURCE_ARGV_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/source/argv)

# ---------------------------------------------------------------------------
# XML source module
#
# A separate target that wraps pugixml. pugixml is PRIVATE-linked here and is
# reachable from nowhere else: the core library has zero parser dependencies, so
# adopters who never use XML never pull pugixml. The module is built only when
# requested (default ON for the test build; adopters opt in). It links the core
# PUBLICly because its public header is a core document_source, and pugixml
# PRIVATELY so the dependency never leaks through the link interface.
# ---------------------------------------------------------------------------
option(NUCLEUS_BUILD_SOURCE_XML "Build the XML source module (fetches pugixml)" ON)

if(NUCLEUS_BUILD_SOURCE_XML)
    include(FetchContent)
    FetchContent_Declare(
        pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml.git
        GIT_TAG v1.15
        FIND_PACKAGE_ARGS NAMES pugixml
    )
    FetchContent_MakeAvailable(pugixml)

    add_library(nucleus_source_xml
        src/nucleus/xml/xml_source.cpp
    )
    add_library(nucleus::source_xml ALIAS nucleus_source_xml)

    target_compile_features(nucleus_source_xml PUBLIC cxx_std_20)
    target_include_directories(nucleus_source_xml
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(nucleus_source_xml
        PUBLIC nucleus::nucleus
        PRIVATE pugixml::pugixml
    )

    install(TARGETS nucleus_source_xml
        EXPORT nucleus-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(NUCLEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NUCLEUS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
