# Per-target module layout. One helper expresses the whole pattern: a module's
# public include root is lib/<name>/include, so every target is consumed via
# #include <nucleus/...> (never a target-prefixed path); compiled modules also see
# lib/<name>/src privately for their internal headers. Every module joins the single
# nucleus-targets export. EXPORT_NAME defaults to the module name; core overrides it
# to `nucleus` so the installed export keeps defining nucleus::nucleus (CMake aliases
# are not exported -- the installed name comes from EXPORT_NAME). The helper is
# itself the worked "how to add a nucleus module" example.

include(GNUInstallDirs)

function(nucleus_add_module name)
    cmake_parse_arguments(ARG "" "TYPE;EXPORT_NAME"
        "SOURCES;LINK_PUBLIC;LINK_PRIVATE;ALIASES" ${ARGN})

    set(target nucleus_${name})
    set(include_root ${CMAKE_CURRENT_SOURCE_DIR}/${name}/include)

    if(ARG_TYPE STREQUAL "INTERFACE")
        # Header-only module: no compiled objects, only an include root.
        add_library(${target} INTERFACE)
        target_compile_features(${target} INTERFACE cxx_std_20)
        target_include_directories(${target} INTERFACE
            $<BUILD_INTERFACE:${include_root}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        if(ARG_LINK_PUBLIC)
            target_link_libraries(${target} INTERFACE ${ARG_LINK_PUBLIC})
        endif()
    else()
        add_library(${target} ${ARG_TYPE} ${ARG_SOURCES})
        nucleus_enable_coverage(${target})
        nucleus_warnings(${target})
        target_compile_features(${target} PUBLIC cxx_std_20)
        # Static archives end up inside consumers' shared libraries (a natural
        # embedding for a configuration engine); without PIC that link fails to
        # relocate on Linux.
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_include_directories(${target}
            PUBLIC
                $<BUILD_INTERFACE:${include_root}>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/${name}/src)
        if(ARG_LINK_PUBLIC)
            target_link_libraries(${target} PUBLIC ${ARG_LINK_PUBLIC})
        endif()
        if(ARG_LINK_PRIVATE)
            target_link_libraries(${target} PRIVATE ${ARG_LINK_PRIVATE})
        endif()
    endif()

    if(NOT ARG_EXPORT_NAME)
        set(ARG_EXPORT_NAME ${name})
    endif()
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${ARG_EXPORT_NAME})

    foreach(alias ${ARG_ALIASES})
        add_library(${alias} ALIAS ${target})
    endforeach()

    # Gated with the aggregate install(EXPORT) in the root rather than instead of it: leaving this
    # ungated would populate an export set nobody emits and still write nucleus headers into
    # whatever prefix the consumer installs into.
    if(NUCLEUS_INSTALL)
        install(TARGETS ${target}
            EXPORT nucleus-targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(DIRECTORY ${include_root}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h")
    endif()
endfunction()

# core: the format-agnostic engine. EXPORT_NAME nucleus preserves nucleus::nucleus
# for existing consumers; nucleus::core is the new explicit alias.
nucleus_add_module(core
    TYPE STATIC
    EXPORT_NAME nucleus
    ALIASES nucleus::nucleus nucleus::core
    SOURCES
        core/src/nucleus/config.cpp
        core/src/nucleus/version.cpp
        core/src/nucleus/keyspace/storage_shape.cpp
        core/src/nucleus/resolve/configuration_space.cpp
        core/src/nucleus/tokenizer/tokenizer.cpp
        core/src/nucleus/tokenizer/token_lexer.cpp
        core/src/nucleus/tokenizer/scope_keys.cpp
        core/src/nucleus/tokenizer/resolver_scope.cpp
        core/src/nucleus/tokenizer/tree_resolver_scope.cpp
        core/src/nucleus/tokenizer/builtin_tokenizers.cpp
        core/src/nucleus/tokenizer/named_args.cpp
        core/src/nucleus/completion/completion.cpp
        core/src/nucleus/query/query.cpp
        core/src/nucleus/query/keyref.cpp
        core/src/nucleus/query/structural_selector.cpp)

set_target_properties(nucleus_core PROPERTIES VERSION ${PROJECT_VERSION})

# The root's std::format probe answered once for this build; carry that answer into a generated
# public header so the format() seam reads it instead of re-testing the consumer's own standard
# library. The template lives outside the include root because that root is installed wholesale.
# BUILD_INTERFACE covers an add_subdirectory consumer; the INSTALL_INTERFACE entry every module
# already carries covers a find_package one, so one include spelling resolves for both.
set(NUCLEUS_USE_STD_FORMAT ${NUCLEUS_HAVE_STD_FORMAT})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/core/format_backend.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/nucleus/detail/format_backend.h
    @ONLY)
target_include_directories(nucleus_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>)
if(NUCLEUS_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/nucleus/detail/format_backend.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nucleus/detail)
endif()

if(NUCLEUS_BUILD_TESTS AND
   (MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    set(read_api_compile_check_tu ${CMAKE_CURRENT_BINARY_DIR}/read_api_compile_check.cpp)
    file(WRITE ${read_api_compile_check_tu}
        "#include <nucleus/config.h>\n#include <nucleus/identity.h>\n"
        "#include <nucleus/config_node.h>\n\n#include <nucleus/query/query.h>\n\n"
        "#include <nucleus/resolve/resolution_context.h>\n\n"
        "#include <nucleus/schema/keyref_pass.h>\n"
        "#include <nucleus/schema/instance_paths.h>\n"
        "#include <nucleus/schema/keyref_candidate_index.h>\n")
    add_library(read_api_compile_check OBJECT
        ${read_api_compile_check_tu}
        core/src/nucleus/config.cpp
        core/src/nucleus/keyspace/storage_shape.cpp
        core/src/nucleus/query/query.cpp
        core/src/nucleus/query/keyref.cpp
        core/src/nucleus/query/structural_selector.cpp)
    nucleus_warnings(read_api_compile_check)
    target_compile_features(read_api_compile_check PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(read_api_compile_check PRIVATE /EHs- /EHc- /GR-)
        target_compile_definitions(read_api_compile_check PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(read_api_compile_check PRIVATE -fno-exceptions -fno-rtti)
    endif()
    target_include_directories(read_api_compile_check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/src)
    target_link_libraries(read_api_compile_check PRIVATE nucleus::nucleus)
endif()

# runtime: a header-only source module. runtime_source builds a configuration
# programmatically (.set(path, value)) with no document or parser dependency. Its
# public adapter header lives under nucleus/runtime/; the INTERFACE link to
# nucleus::nucleus means a consumer of nucleus::runtime also gets core.
nucleus_add_module(runtime
    TYPE INTERFACE
    ALIASES nucleus::runtime
    LINK_PUBLIC nucleus::nucleus)

# env: a header-only flat-source module. env_source carries host-mapped (path, value)
# pairs and claims no structural capabilities (no nesting, no repeated keys, no typed
# scalars). Built unconditionally: header-only modules have zero link cost for targets
# that do not use them; they compile away when not included.
nucleus_add_module(env
    TYPE INTERFACE
    ALIASES nucleus::env
    LINK_PUBLIC nucleus::nucleus)

# argv: a header-only CLI-argument source module. argv_source parses `--flag=value`
# tokens via the `-` <-> `/` bijection (cli_surface.h) and emits flat keyspace entries.
# Also header-only, so no link cost for non-users. LINK_PUBLIC nucleus::nucleus brings
# the core include root (key_path, capability, configuration_source seam) in transitively.
nucleus_add_module(argv
    TYPE INTERFACE
    ALIASES nucleus::argv
    LINK_PUBLIC nucleus::nucleus)

# Tidy anchors for the header-only modules (runtime/env/argv). Those modules ship
# no compiled translation unit, so their public headers never enter
# compile_commands.json and escape clang-tidy. One OBJECT TU per module includes
# those headers, landing a lib/<module>/src/nucleus/<module>/ database entry the
# tidy header filter matches. Plain OBJECT targets, never routed through
# nucleus_add_module (which installs and exports); they are internal-only and gated
# to the dev/test configure so a downstream add_subdirectory consumer never
# compiles them, while CI (configured with tests on) still lands the DB entries.
if(NUCLEUS_BUILD_TESTS)
    add_library(nucleus_runtime_tidy_anchor OBJECT
        runtime/src/nucleus/runtime/tidy_anchor.cpp)
    target_link_libraries(nucleus_runtime_tidy_anchor PRIVATE nucleus::runtime)

    add_library(nucleus_env_tidy_anchor OBJECT
        env/src/nucleus/env/tidy_anchor.cpp)
    target_link_libraries(nucleus_env_tidy_anchor PRIVATE nucleus::env)

    add_library(nucleus_argv_tidy_anchor OBJECT
        argv/src/nucleus/argv/tidy_anchor.cpp)
    target_link_libraries(nucleus_argv_tidy_anchor PRIVATE nucleus::argv)
endif()

# Diagnostic formatting backend: when the standard library lacks std::format the
# format() seam falls back to {fmt} in a public header, so the link is PUBLIC. The
# probe + fetch run in the root before add_subdirectory(lib); here we only wire the
# link once nucleus_core exists.
if(NOT NUCLEUS_HAVE_STD_FORMAT)
    target_link_libraries(nucleus_core PUBLIC fmt::fmt)
endif()

# xml: a format module that wraps pugixml. pugixml is PRIVATE-linked and reachable
# only inside lib/xml/src, so core (which cannot even see this target) never pulls
# it. Public adapter headers live under nucleus/xml/. Built on demand.
if(NUCLEUS_BUILD_SOURCE_XML)
    include(FetchContent)
    FetchContent_Declare(
        pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml.git
        # v1.15, pinned to the commit so a moved tag cannot change the build
        GIT_TAG ee86beb30e4973f5feffe3ce63bfa4fbadf72f38
        # SYSTEM: pugixml headers are third-party; consume them as system
        # includes so their internals never trip our curated -Weverything/-Werror.
        SYSTEM
        FIND_PACKAGE_ARGS NAMES pugixml
    )
    FetchContent_MakeAvailable(pugixml)

    nucleus_add_module(xml
        TYPE STATIC
        EXPORT_NAME xml
        ALIASES nucleus::xml
        SOURCES
            xml/src/nucleus/xml/xml_source.cpp
            xml/src/nucleus/xml/xml_emitter.cpp
            xml/src/nucleus/xml/xml_compatibility.cpp
            xml/src/nucleus/xml/xml_template_renderer.cpp
            xml/src/nucleus/xml/xml_document_renderer.cpp
        LINK_PUBLIC nucleus::nucleus
        LINK_PRIVATE pugixml::pugixml)

    if(NUCLEUS_BUILD_TESTS AND
       (MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        set(emit_api_compile_check_tu
            ${CMAKE_CURRENT_BINARY_DIR}/emit_api_compile_check.cpp)
        file(WRITE ${emit_api_compile_check_tu}
            "#include <nucleus/xml/xml_emitter.h>\n")
        add_library(emit_api_compile_check OBJECT
            ${emit_api_compile_check_tu})
        nucleus_warnings(emit_api_compile_check)
        target_compile_features(emit_api_compile_check PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(emit_api_compile_check PRIVATE /EHs- /EHc- /GR-)
            target_compile_definitions(emit_api_compile_check PRIVATE _HAS_EXCEPTIONS=0)
        else()
            target_compile_options(emit_api_compile_check PRIVATE -fno-exceptions -fno-rtti)
        endif()
        target_include_directories(emit_api_compile_check PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/xml/include
            ${CMAKE_CURRENT_SOURCE_DIR}/xml/src)
        target_link_libraries(emit_api_compile_check
            PRIVATE nucleus::nucleus pugixml::pugixml)
    endif()

    # The package config (root scope) requires pugixml when the module is installed.
    set(NUCLEUS_XML_INSTALLED TRUE PARENT_SCOPE)
endif()
