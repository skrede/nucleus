# Test framework: Catch2. Fetched via FetchContent by default so the walking
# skeleton builds anywhere; a vcpkg/find_package path can replace this later
# (scaffold default -- confirm during the project's tooling decision).
if(NUCLEUS_FETCH_TEST_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.0
    )
    FetchContent_MakeAvailable(Catch2)
else()
    find_package(Catch2 3 REQUIRED)
endif()

# Registers a Catch2 test executable built from a single source file, linked
# against the core library and Catch2's bundled main, and wired into CTest. The
# core's internal tree (lib/core/src) is on the include path so isolation tests
# can construct the registries directly.
function(nucleus_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus Catch2::Catch2WithMain)
    nucleus_enable_coverage(${name})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nucleus_add_test(version_test)
nucleus_add_test(expected_test)
nucleus_add_test(type_info_test)
nucleus_add_test(format_test)
nucleus_add_test(identity_test)
nucleus_add_test(log_sink_test)
nucleus_add_test(facade_test)
nucleus_add_test(facade_schema_test)
nucleus_add_test(registry_isolation_test)
nucleus_add_test(source_seam_test)
nucleus_add_test(capability_gating_test)
nucleus_add_test(capability_requirements_test)
nucleus_add_test(auto_gating_test)
nucleus_add_test(discovery_test)
nucleus_add_test(key_path_test)
nucleus_add_test(keyspace_test)
nucleus_add_test(schema_registry_test)
nucleus_add_test(schema_enforcer_test)
nucleus_add_test(argv_source_test)
nucleus_add_test(schema_cli_bijection_test)
nucleus_add_test(token_lexer_test)
nucleus_add_test(token_resolution_test)
nucleus_add_test(token_cycle_test)
nucleus_add_test(resolution_test)
nucleus_add_test(diagnostics_test)
nucleus_add_test(completion_test)
nucleus_add_test(completion_smoke_test)
nucleus_add_test(facade_completion_test)
nucleus_add_test(strain_scope_policy_test)
nucleus_add_test(converter_registry_test)
nucleus_add_test(builder_lifecycle_test)
nucleus_add_test(expand_independence_test)
nucleus_add_test(concurrent_load_test)

# The concurrent-load guarantee test spawns std::threads; link the platform's
# threading library so the shared-const-read design is exercised under ASan.
find_package(Threads REQUIRED)
target_link_libraries(concurrent_load_test PRIVATE Threads::Threads)

# ---------------------------------------------------------------------------
# Buffer-lifetime test: exercises the XML document source through the seam and
# proves the copy-out severs every view from the document arena before it is
# dropped. Links the XML module (and thus pugixml, transitively) only here, never
# in core. Built only when the XML module is. Run under the sanitizer to make the
# use-after-free guard real.
# ---------------------------------------------------------------------------
if(NUCLEUS_BUILD_SOURCE_XML)
    # Template emission lives in the xml module now (nucleus::xml::emit_template),
    # so this test links the module; its header arrives via the nucleus::xml link.
    add_executable(xml_template_test xml_template_test.cpp)
    nucleus_enable_coverage(xml_template_test)
    target_link_libraries(xml_template_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(xml_template_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME xml_template_test COMMAND xml_template_test)

    # The emit-seam proof builds all three emitters (env, args, xml) from one space,
    # so it links the xml module; its header arrives via the nucleus::xml link.
    add_executable(emit_seam_test emit_seam_test.cpp)
    nucleus_enable_coverage(emit_seam_test)
    target_link_libraries(emit_seam_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(emit_seam_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME emit_seam_test COMMAND emit_seam_test)

    add_executable(buffer_lifetime_test buffer_lifetime_test.cpp)
    nucleus_enable_coverage(buffer_lifetime_test)
    target_link_libraries(buffer_lifetime_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(buffer_lifetime_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME buffer_lifetime_test COMMAND buffer_lifetime_test)

    # Convergence buffer-drop checkpoint: drives the full facade resolve() with an
    # XML document source in the precedence stack, drops every buffer, then reads
    # every value back. The single most valuable test in the project under ASan.
    add_executable(resolve_buffer_drop_test resolve_buffer_drop_test.cpp)
    nucleus_enable_coverage(resolve_buffer_drop_test)
    target_link_libraries(resolve_buffer_drop_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(resolve_buffer_drop_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME resolve_buffer_drop_test COMMAND resolve_buffer_drop_test)

    # Keyed-projection test: the schema declares a container's primary key and the
    # XML source must render repeated instances as distinct keyed subtrees instead
    # of collapsing them last-wins. Links the XML module for a real document walk.
    add_executable(keyed_projection_test keyed_projection_test.cpp)
    nucleus_enable_coverage(keyed_projection_test)
    target_link_libraries(keyed_projection_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(keyed_projection_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME keyed_projection_test COMMAND keyed_projection_test)

    add_executable(keyed_selection_test keyed_selection_test.cpp)
    nucleus_enable_coverage(keyed_selection_test)
    target_link_libraries(keyed_selection_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(keyed_selection_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME keyed_selection_test COMMAND keyed_selection_test)

    add_executable(inherit_chain_test inherit_chain_test.cpp)
    nucleus_enable_coverage(inherit_chain_test)
    target_link_libraries(inherit_chain_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(inherit_chain_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME inherit_chain_test COMMAND inherit_chain_test)

    # XML persistence round-trip: load a configuration from XML, write it back via
    # the pugixml WRITE API inside the xml module, and re-load it -- keys, scalar
    # values, and repeated collections must survive intact.
    add_executable(xml_persist_test xml_persist_test.cpp)
    nucleus_enable_coverage(xml_persist_test)
    target_link_libraries(xml_persist_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(xml_persist_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME xml_persist_test COMMAND xml_persist_test)

    add_executable(integration_shape_test integration_shape_test.cpp)
    nucleus_enable_coverage(integration_shape_test)
    target_link_libraries(integration_shape_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(integration_shape_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME integration_shape_test COMMAND integration_shape_test)

    # Repeated-values mode: schema flag, fold accumulation/replacement, get_all(),
    # relay through keyed containers, capability gating, scope-policy exclusion.
    add_executable(repeated_element_test repeated_element_test.cpp)
    nucleus_enable_coverage(repeated_element_test)
    target_link_libraries(repeated_element_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(repeated_element_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME repeated_element_test COMMAND repeated_element_test)

    # Typed-field converter seam: built-in scalar edge cases, custom converter,
    # conversion failure surfacing, get_as error distinctions, repeated x typed,
    # orthogonality checks. Links the XML module for document-driven resolution.
    add_executable(typed_element_test typed_element_test.cpp)
    nucleus_enable_coverage(typed_element_test)
    target_link_libraries(typed_element_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(typed_element_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME typed_element_test COMMAND typed_element_test)

    # Typed-seam interaction shapes: typed x inheritance chain, typed x pruned
    # strain, typed x repeated across layers, typed x scope policy exclusion,
    # and access surface agreement. Complements typed_element_test.cpp with
    # composition-model integration rather than unit behavior.
    add_executable(typed_shape_test typed_shape_test.cpp)
    nucleus_enable_coverage(typed_shape_test)
    target_link_libraries(typed_shape_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(typed_shape_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME typed_shape_test COMMAND typed_shape_test)

    # Inheritance golden harness: drives real fixture documents through the public
    # load_configuration() surface and diffs the serialized resolved keyspace against
    # pre-committed goldens derived from nucleus's own semantics. Cases are discovered
    # by directory scan under the fixture dir, so new topology fixtures are added as
    # data files with no source/CMake edit. Runs in the default ctest set (this
    # XML-module block), making it the merge-blocking resolution gate.
    add_executable(inheritance_golden_test inheritance_golden/inheritance_golden_test.cpp)
    nucleus_enable_coverage(inheritance_golden_test)
    target_link_libraries(inheritance_golden_test
        PRIVATE nucleus::nucleus nucleus::source_xml Catch2::Catch2WithMain)
    target_include_directories(inheritance_golden_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
        ${CMAKE_CURRENT_SOURCE_DIR}/inheritance_golden
    )
    target_compile_definitions(inheritance_golden_test PRIVATE
        NUCLEUS_GOLDEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/inheritance_golden/fixtures")
    add_test(NAME inheritance_golden_test COMMAND inheritance_golden_test)
endif()

# ---------------------------------------------------------------------------
# Negative compile tests: registries that violate the flat-ownership invariant
# must NOT compile. WILL_FAIL asserts the build of each fixture fails, making the
# invariant executable rather than review-only. Two distinct vectors are covered:
#   - a sibling REFERENCE member (breaks default-construction);
#   - a default-constructible registry that still exposes a constructor taking a
#     sibling by pointer (a construction-time hand-off the strengthened pin
#     rejects even though the plain concept would pass it).
# ---------------------------------------------------------------------------
add_test(
    NAME flat_ownership_negative_compile
    COMMAND ${CMAKE_COMMAND} --build . --target sibling_member_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(flat_ownership_negative_compile PROPERTIES WILL_FAIL TRUE)

add_executable(sibling_member_fixture EXCLUDE_FROM_ALL
    isolation/sibling_member_fixture.cpp)
target_link_libraries(sibling_member_fixture PRIVATE nucleus::nucleus)
target_include_directories(sibling_member_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

add_test(
    NAME flat_ownership_entangling_ctor_negative_compile
    COMMAND ${CMAKE_COMMAND} --build . --target entangling_ctor_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(flat_ownership_entangling_ctor_negative_compile PROPERTIES WILL_FAIL TRUE)

add_executable(entangling_ctor_fixture EXCLUDE_FROM_ALL
    isolation/entangling_ctor_fixture.cpp)
target_link_libraries(entangling_ctor_fixture PRIVATE nucleus::nucleus)
target_include_directories(entangling_ctor_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

# ---------------------------------------------------------------------------
# Core-purity check: the core must carry no format/host vocabulary. Greps the
# core headers/sources for xml/pugi/host symbols; nonzero hits fail the test.
# ---------------------------------------------------------------------------
add_test(
    NAME core_purity_check
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/scripts/core_purity_check.cmake
)

# ---------------------------------------------------------------------------
# ASan trip test: built and registered ONLY when the sanitizer is enabled. It
# deliberately reads freed heap memory to confirm ASan is wired and tripping; it
# is never part of a normal (non-sanitizer) run.
# ---------------------------------------------------------------------------
if(NUCLEUS_BUILD_SANITIZER)
    add_executable(asan_trip_test asan_trip_test.cpp)
    target_link_libraries(asan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME asan_trip_test COMMAND asan_trip_test)
    set_tests_properties(asan_trip_test PROPERTIES
        PASS_REGULAR_EXPRESSION "heap-use-after-free|AddressSanitizer")
endif()
