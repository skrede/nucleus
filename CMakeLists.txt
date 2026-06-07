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
# nucleus core library
#
# The public headers below are the stable, installed surface of the engine; the
# subdirectory header sets that follow carry the source-seam, keyspace, schema,
# and diagnostics contracts.
# ---------------------------------------------------------------------------
set(NUCLEUS_PUBLIC_HEADERS
    include/nucleus/version.h
    include/nucleus/result.h
    include/nucleus/format.h
    include/nucleus/configuration_space.h
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
    include/nucleus/source/discovery.h
    include/nucleus/source/path_text.h
    include/nucleus/source/feature_gate.h
    include/nucleus/source/parser_adapter.h
    include/nucleus/source/document_source.h
    include/nucleus/source/extension_registry.h
)

set(NUCLEUS_SOURCE_ENV_HEADERS
    include/nucleus/source/env/env_source.h
)

set(NUCLEUS_KEYSPACE_HEADERS
    include/nucleus/keyspace/value.h
    include/nucleus/keyspace/entry.h
    include/nucleus/keyspace/key_path.h
    include/nucleus/keyspace/keyspace.h
    include/nucleus/keyspace/provenance.h
)

set(NUCLEUS_ENTRY_HEADERS
    include/nucleus/entry/precedence.h
    include/nucleus/entry/configuration.h
)

set(NUCLEUS_DIAGNOSTICS_HEADERS
    include/nucleus/diagnostics/key_suggester.h
    include/nucleus/diagnostics/conflict_report.h
)

set(NUCLEUS_SCHEMA_HEADERS
    include/nucleus/schema/anchor.h
    include/nucleus/schema/schema.h
)

set(NUCLEUS_SOURCE_ARGV_HEADERS
    include/nucleus/source/argv/cli_surface.h
    include/nucleus/source/argv/argv_source.h
)

set(NUCLEUS_COMPLETION_HEADERS
    include/nucleus/completion/completion.h
    include/nucleus/completion/completion_model.h
    include/nucleus/completion/shell_emitter.h
    include/nucleus/completion/bash_emitter.h
    include/nucleus/completion/zsh_emitter.h
)

set(NUCLEUS_SOURCES
    src/nucleus/version.cpp
    src/nucleus/entry/configuration_space.cpp
    src/nucleus/tokenizer/tokenizer.cpp
    src/nucleus/tokenizer/token_lexer.cpp
    src/nucleus/tokenizer/scope_keys.cpp
    src/nucleus/tokenizer/resolver_scope.cpp
    src/nucleus/tokenizer/uuid.cpp
    src/nucleus/tokenizer/builtin_tokenizers.cpp
    src/nucleus/completion/completion.cpp
)

add_library(nucleus ${NUCLEUS_SOURCES})
add_library(nucleus::nucleus ALIAS nucleus)
nucleus_enable_coverage(nucleus)

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
# Whether the XML module (and thus its pugixml dependency) is part of this
# install. Defaults off; set TRUE when the module is built+installed below. The
# package config consumes it to decide whether to find_dependency(pugixml).
set(NUCLEUS_XML_INSTALLED FALSE)

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
install(FILES ${NUCLEUS_ENTRY_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/entry)
install(FILES ${NUCLEUS_DIAGNOSTICS_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/diagnostics)
install(FILES ${NUCLEUS_SCHEMA_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/schema)
install(FILES ${NUCLEUS_SOURCE_ARGV_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/source/argv)
install(FILES ${NUCLEUS_COMPLETION_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/completion)

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
    nucleus_enable_coverage(nucleus_source_xml)

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

    # Match the imported target name to the in-tree alias (nucleus::source_xml).
    # Without this the NAMESPACE-prefixed export yields nucleus::nucleus_source_xml.
    set_target_properties(nucleus_source_xml PROPERTIES EXPORT_NAME source_xml)

    install(TARGETS nucleus_source_xml
        EXPORT nucleus-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # The installed export carries $<LINK_ONLY:pugixml::pugixml> from this
    # static module, so the package config must require pugixml. Recorded here
    # and consumed by the config template below.
    set(NUCLEUS_XML_INSTALLED TRUE)
endif()

# ---------------------------------------------------------------------------
# HOST tokenizer module (opt-in)
#
# An optional, separately-linked module that adds the ${HOST.*} tokenizer
# (hostname / fqdn / machine-id / username). It pulls platform-specific code,
# which is exactly why it is isolated here and OFF by default: core carries no
# platform host vocabulary, and a host that wants these tokens links this module
# and registers make_host_tokenizer() explicitly. It links core PUBLICly because
# its public surface is a core tokenizer; it has no third-party dependency.
# ---------------------------------------------------------------------------
option(NUCLEUS_BUILD_TOKENIZER_HOST "Build the opt-in HOST tokenizer module" OFF)

if(NUCLEUS_BUILD_TOKENIZER_HOST)
    add_library(nucleus_tokenizer_host
        src/nucleus/host/host_platform.cpp
        src/nucleus/host/host_tokenizer.cpp
    )
    add_library(nucleus::tokenizer_host ALIAS nucleus_tokenizer_host)
    nucleus_enable_coverage(nucleus_tokenizer_host)

    target_compile_features(nucleus_tokenizer_host PUBLIC cxx_std_20)
    target_include_directories(nucleus_tokenizer_host
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(nucleus_tokenizer_host PUBLIC nucleus::nucleus)

    # Match the imported target name to the in-tree alias (nucleus::tokenizer_host).
    set_target_properties(nucleus_tokenizer_host PROPERTIES EXPORT_NAME tokenizer_host)

    if(WIN32)
        target_link_libraries(nucleus_tokenizer_host PRIVATE ws2_32)
    endif()

    install(TARGETS nucleus_tokenizer_host
        EXPORT nucleus-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

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
