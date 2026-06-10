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
        add_library(${target} ${ARG_SOURCES})
        nucleus_enable_coverage(${target})
        target_compile_features(${target} PUBLIC cxx_std_20)
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

    install(TARGETS ${target}
        EXPORT nucleus-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${include_root}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endfunction()

# core: the format-agnostic engine. EXPORT_NAME nucleus preserves nucleus::nucleus
# for existing consumers; nucleus::core is the new explicit alias.
nucleus_add_module(core
    TYPE STATIC
    EXPORT_NAME nucleus
    ALIASES nucleus::nucleus nucleus::core
    SOURCES
        core/src/nucleus/version.cpp
        core/src/nucleus/resolve/configuration_space.cpp
        core/src/nucleus/tokenizer/tokenizer.cpp
        core/src/nucleus/tokenizer/token_lexer.cpp
        core/src/nucleus/tokenizer/scope_keys.cpp
        core/src/nucleus/tokenizer/resolver_scope.cpp
        core/src/nucleus/tokenizer/builtin_tokenizers.cpp
        core/src/nucleus/completion/completion.cpp)

set_target_properties(nucleus_core PROPERTIES VERSION ${PROJECT_VERSION})

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
        GIT_TAG v1.15
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
        LINK_PUBLIC nucleus::nucleus
        LINK_PRIVATE pugixml::pugixml)

    # The package config (root scope) requires pugixml when the module is installed.
    set(NUCLEUS_XML_INSTALLED TRUE PARENT_SCOPE)
endif()
