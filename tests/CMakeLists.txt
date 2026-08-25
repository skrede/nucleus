# Test framework: Catch2. Fetched via FetchContent by default so the walking
# skeleton builds anywhere; a vcpkg/find_package path can replace this later
# (scaffold default -- confirm during the project's tooling decision).
if(NUCLEUS_FETCH_TEST_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        # v3.15.0, pinned to the commit so a moved tag cannot change the build
        GIT_TAG 6ee0826dcae55ed1e06b2c5701981221e979e1e6
        # SYSTEM: Catch2's headers are consumed as system includes so their own
        # diagnostics (e.g. the float compare inside the REQUIRE decomposer) do
        # not surface under our curated -Weverything/-Werror in test TUs.
        SYSTEM
    )
    FetchContent_MakeAvailable(Catch2)
else()
    find_package(Catch2 3 REQUIRED)
endif()

# include() keeps one directory scope for the whole test tree, so a source path relative to the
# including fragment would resolve against tests/ instead. Each fragment names its own folder in
# nucleus_test_dir and unsets it at the end, so a fragment that forgets to set it trips the fatal
# error below rather than silently inheriting its predecessor's folder.
function(nucleus_test_sources out_var name)
    if(NOT DEFINED nucleus_test_dir)
        message(FATAL_ERROR "nucleus_add_test(${name}): the including fragment set no nucleus_test_dir")
    endif()
    set(sources "")
    foreach(source IN LISTS ARGN)
        list(APPEND sources ${nucleus_test_dir}/${source})
    endforeach()
    if(NOT sources)
        set(sources ${nucleus_test_dir}/${name}.cpp)
    endif()
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

# With no keyword present every trailing argument lands in arg_UNPARSED_ARGUMENTS, so the bare
# form still means "link these libraries". The core's private tree and tests/ itself are on every
# target's include path, so isolation tests can construct the registries directly and a quoted
# include written as a path from tests/ resolves from any folder at any depth.
function(nucleus_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINK;INCLUDE;DEFINES;PROPERTIES")
    nucleus_test_sources(sources ${name} ${arg_SOURCES})
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE
        nucleus::nucleus ${arg_LINK} ${arg_UNPARSED_ARGUMENTS} Catch2::Catch2WithMain)
    nucleus_enable_coverage(${name})
    nucleus_warnings(${name})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${arg_INCLUDE})
    if(arg_DEFINES)
        target_compile_definitions(${name} PRIVATE ${arg_DEFINES})
    endif()
    add_test(NAME ${name} COMMAND ${name})
    if(arg_PROPERTIES)
        set_tests_properties(${name} PROPERTIES ${arg_PROPERTIES})
    endif()
endfunction()

set(nucleus_test_dir .)

nucleus_add_test(identity_test)
nucleus_add_test(facade_test nucleus::env)
nucleus_add_test(facade_schema_test nucleus::runtime)
nucleus_add_test(registration_policy_test)
nucleus_add_test(source_seam_test)
nucleus_add_test(source_handle_test)
nucleus_add_test(source_concept_test)
nucleus_add_test(source_stack_test)
nucleus_add_test(discovery_test)
nucleus_add_test(key_path_test)
nucleus_add_test(path_text_test)
nucleus_add_test(keyspace_test)
nucleus_add_test(config_storage_shape_test nucleus::runtime)
nucleus_add_test(schema_registry_test)
nucleus_add_test(schema_enforcer_test)
nucleus_add_test(constraint_group_test nucleus::runtime)
nucleus_add_test(identity_group_test nucleus::runtime)
nucleus_add_test(keyed_composition_test nucleus::runtime nucleus::argv nucleus::env)
nucleus_add_test(keyref_test nucleus::runtime)
nucleus_add_test(retrieval_error_test nucleus::runtime)
if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(argv_source_test nucleus::argv nucleus::xml nucleus::runtime)
    nucleus_add_test(retrieval_addressing_test nucleus::runtime nucleus::xml nucleus::argv)
    nucleus_add_test(strain_scope_policy_test nucleus::runtime nucleus::xml)
endif()
nucleus_add_test(schema_cli_bijection_test nucleus::argv)
nucleus_add_test(multispace_argv_test nucleus::argv)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit_gap_tests.cmake)
nucleus_add_test(flat_emit_round_trip_test nucleus::env nucleus::argv nucleus::runtime)
nucleus_add_test(emitter_contract_test nucleus::env nucleus::argv)
nucleus_add_test(token_lexer_test)
nucleus_add_test(token_resolution_test)
nucleus_add_test(named_args_test)
nucleus_add_test(token_cycle_test)
nucleus_add_test(facade_completion_test)
nucleus_add_test(converter_registry_test nucleus::env)
nucleus_add_test(builder_lifecycle_test)
nucleus_add_test(concurrent_load_test nucleus::runtime)
nucleus_add_test(concurrent_collection_load_test nucleus::runtime)
nucleus_add_test(load_front_door_test nucleus::runtime)
nucleus_add_test(reference_resolution_test nucleus::runtime)
nucleus_add_test(reference_cycle_test nucleus::runtime)
nucleus_add_test(reference_fallback_test nucleus::runtime)
nucleus_add_test(pkey_tokenizer_test nucleus::runtime)
nucleus_add_test(recognizer_of_test nucleus::argv)
nucleus_add_test(converter_edge_cases_test)
# Forces the strtof/strtod fallback so it is exercised on every platform, not
# only Apple libc++; the test switches the thread locale to a comma-decimal one.
nucleus_add_test(converter_locale_test)
target_compile_definitions(converter_locale_test PRIVATE
    NUCLEUS_FORCE_FP_FROM_CHARS_FALLBACK)
# Catch2 exits 4 when every test skips (here: no comma-decimal locale installed);
# tell CTest that is a skip, not a failure.
set_tests_properties(converter_locale_test PROPERTIES SKIP_RETURN_CODE 4)
nucleus_add_test(selector_test nucleus::runtime)
nucleus_add_test(selector_combinator_test nucleus::runtime)
nucleus_add_test(selector_schema_role_test nucleus::runtime)
nucleus_add_test(selector_owner_test nucleus::runtime)
nucleus_add_test(selector_strain_test nucleus::runtime)
nucleus_add_test(selector_lifetime_test nucleus::runtime)

# The concurrent-load guarantee tests spawn std::threads; link the platform's
# threading library so the shared-const-read design is exercised under ASan.
find_package(Threads REQUIRED)
target_link_libraries(concurrent_load_test PRIVATE Threads::Threads)
target_link_libraries(concurrent_collection_load_test PRIVATE Threads::Threads)

# ---------------------------------------------------------------------------
# Buffer-lifetime test: exercises the XML document source through the seam and
# proves the copy-out severs every view from the document arena before it is
# dropped. Links the XML module (and thus pugixml, transitively) only here, never
# in core. Built only when the XML module is. Run under the sanitizer to make the
# use-after-free guard real.
# ---------------------------------------------------------------------------
if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(instance_addressing_test nucleus::runtime nucleus::xml nucleus::argv)
    add_executable(identity_envelope_test
        identity_envelope_test.cpp
        identity_envelope_emit_test.cpp
        identity_envelope_inheritance_test.cpp)
    nucleus_enable_coverage(identity_envelope_test)
    nucleus_warnings(identity_envelope_test)
    target_link_libraries(identity_envelope_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::argv Catch2::Catch2WithMain)
    target_include_directories(identity_envelope_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME identity_envelope_test COMMAND identity_envelope_test)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/xml_emit_gap_tests.cmake)

    # Template emission lives in the xml module now (nucleus::xml::emit_template),
    # so this test links the module; its header arrives via the nucleus::xml link.
    add_executable(xml_template_test xml_template_test.cpp)
    nucleus_enable_coverage(xml_template_test)
    nucleus_warnings(xml_template_test)
    target_link_libraries(xml_template_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    target_include_directories(xml_template_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME xml_template_test COMMAND xml_template_test)

    # The emit-seam proof builds all three emitters (env, args, xml) from one space,
    # so it links all three modules; headers arrive via the respective links.
    add_executable(emit_seam_test emit_seam_test.cpp)
    nucleus_enable_coverage(emit_seam_test)
    nucleus_warnings(emit_seam_test)
    target_link_libraries(emit_seam_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::env nucleus::argv Catch2::Catch2WithMain)
    target_include_directories(emit_seam_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME emit_seam_test COMMAND emit_seam_test)

    add_executable(buffer_lifetime_test buffer_lifetime_test.cpp)
    nucleus_enable_coverage(buffer_lifetime_test)
    nucleus_warnings(buffer_lifetime_test)
    target_link_libraries(buffer_lifetime_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    target_include_directories(buffer_lifetime_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME buffer_lifetime_test COMMAND buffer_lifetime_test)

    add_executable(xml_robustness_test xml_robustness_test.cpp)
    nucleus_enable_coverage(xml_robustness_test)
    nucleus_warnings(xml_robustness_test)
    target_link_libraries(xml_robustness_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    add_test(NAME xml_robustness_test COMMAND xml_robustness_test)

    # Keyed-projection test: the schema declares a container's primary key and the
    # XML source must render repeated instances as distinct keyed subtrees instead
    # of collapsing them last-wins. Links the XML module for a real document walk.
    add_executable(keyed_projection_test keyed_projection_test.cpp)
    nucleus_enable_coverage(keyed_projection_test)
    nucleus_warnings(keyed_projection_test)
    target_link_libraries(keyed_projection_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    target_include_directories(keyed_projection_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME keyed_projection_test COMMAND keyed_projection_test)

    add_executable(keyed_selection_test keyed_selection_test.cpp)
    nucleus_enable_coverage(keyed_selection_test)
    nucleus_warnings(keyed_selection_test)
    target_link_libraries(keyed_selection_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    target_include_directories(keyed_selection_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME keyed_selection_test COMMAND keyed_selection_test)

    # pkey-identity-as-data acceptance suite.
    add_executable(pkey_identity_test
        pkey_identity_test.cpp
        pkey_identity_read_test.cpp)
    nucleus_enable_coverage(pkey_identity_test)
    nucleus_warnings(pkey_identity_test)
    target_link_libraries(pkey_identity_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::runtime Catch2::Catch2WithMain)
    target_include_directories(pkey_identity_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME pkey_identity_test COMMAND pkey_identity_test)

    nucleus_add_test(xml_identity_contract_test nucleus::xml)

    add_executable(inherit_chain_test inherit_chain_test.cpp)
    nucleus_enable_coverage(inherit_chain_test)
    nucleus_warnings(inherit_chain_test)
    target_link_libraries(inherit_chain_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::env Catch2::Catch2WithMain)
    target_include_directories(inherit_chain_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME inherit_chain_test COMMAND inherit_chain_test)

    # Location-token wiring test: two XML files in different temp directories each resolve
    # ${dir.path} to their own directory, proving per-source file-frame binding.
    add_executable(location_token_wiring_test location_token_wiring_test.cpp)
    nucleus_enable_coverage(location_token_wiring_test)
    nucleus_warnings(location_token_wiring_test)
    target_link_libraries(location_token_wiring_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    target_include_directories(location_token_wiring_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME location_token_wiring_test COMMAND location_token_wiring_test)

    add_executable(xml_persist_test
        xml_persist_test.cpp
        xml_persist_render_test.cpp
        xml_persist_value_test.cpp)
    nucleus_enable_coverage(xml_persist_test)
    nucleus_warnings(xml_persist_test)
    target_link_libraries(xml_persist_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
    target_include_directories(xml_persist_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME xml_persist_test COMMAND xml_persist_test)

    add_executable(xml_repeated_container_test
        xml_repeated_container_test.cpp
        xml_repeated_round_trip_test.cpp
        xml_repeated_contract_test.cpp
        xml_repeated_value_test.cpp)
    nucleus_enable_coverage(xml_repeated_container_test)
    nucleus_warnings(xml_repeated_container_test)
    target_link_libraries(xml_repeated_container_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::runtime Catch2::Catch2WithMain)
    target_include_directories(xml_repeated_container_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME xml_repeated_container_test COMMAND xml_repeated_container_test)

    add_executable(integration_shape_test integration_shape_test.cpp)
    nucleus_enable_coverage(integration_shape_test)
    nucleus_warnings(integration_shape_test)
    target_link_libraries(integration_shape_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::env Catch2::Catch2WithMain)
    target_include_directories(integration_shape_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME integration_shape_test COMMAND integration_shape_test)

    # Unified fold: repeated containers and repeated leaves stored as indexed scalars;
    # replace_by_ordinal across layers; extend= guard; get_all numeric ordinal order.
    add_executable(repeated_container_test repeated_container_test.cpp)
    nucleus_enable_coverage(repeated_container_test)
    nucleus_warnings(repeated_container_test)
    target_link_libraries(repeated_container_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::runtime Catch2::Catch2WithMain)
    target_include_directories(repeated_container_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME repeated_container_test COMMAND repeated_container_test)

    # Repeated-values mode: schema flag, fold accumulation/replacement, get_all(),
    # relay through keyed containers, capability gating, scope-policy exclusion.
    add_executable(repeated_element_test repeated_element_test.cpp)
    nucleus_enable_coverage(repeated_element_test)
    nucleus_warnings(repeated_element_test)
    target_link_libraries(repeated_element_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::runtime nucleus::env Catch2::Catch2WithMain)
    target_include_directories(repeated_element_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME repeated_element_test COMMAND repeated_element_test)

    # Typed-field converter seam: built-in scalar edge cases, custom converter,
    # conversion failure surfacing, get_as error distinctions, repeated x typed,
    # orthogonality checks. Links the XML module for document-driven resolution.
    add_executable(typed_element_test typed_element_test.cpp)
    nucleus_enable_coverage(typed_element_test)
    nucleus_warnings(typed_element_test)
    target_link_libraries(typed_element_test
        PRIVATE nucleus::nucleus nucleus::xml Catch2::Catch2WithMain)
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
    nucleus_warnings(typed_shape_test)
    target_link_libraries(typed_shape_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::env Catch2::Catch2WithMain)
    target_include_directories(typed_shape_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME typed_shape_test COMMAND typed_shape_test)

    # Collection shapes: nested repetition under sibling outer instances, repeated
    # leaves inside and outside a repeated container, and layered sources over a
    # single document -- asserting both the resolved values and the provenance the
    # untouched instances keep; plus the addressing rule that rejects a path naming
    # no instance, ordinal repetition beneath a primary-keyed container, the pool a
    # unique value competes in, the enclosing-instance pool for identity values,
    # stable first-appearance ordering, typed failure channels and same-layer duplicate diagnostics for keyed merges,
    # per-instance displacement within a selected strain, stable compaction within
    # each enclosing instance, and one document combining those shapes across a
    # load, an emit and a load again. Fixture documents live beside the test.
    add_executable(collection_shapes_test
        collection_shapes/collection_shapes_test.cpp
        collection_shapes/addressing_test.cpp
        collection_shapes/keyed_ordinal_test.cpp
        collection_shapes/keyed_merge_stability_test.cpp
        collection_shapes/keyed_duplicate_test.cpp
        collection_shapes/keyed_mixed_source_test.cpp
        collection_shapes/error_channel_test.cpp
        collection_shapes/keyref_repeated_namespace_test.cpp
        collection_shapes/strain_displacement_test.cpp
        collection_shapes/strain_compaction_test.cpp
        collection_shapes/strain_compaction_boundary_test.cpp
        collection_shapes/strain_ordering_test.cpp
        collection_shapes/transparent_root_test.cpp
        collection_shapes/instance_presence_test.cpp
        collection_shapes/violation_report_test.cpp
        collection_shapes/unique_scope_test.cpp
        collection_shapes/identity_pool_scope_test.cpp
        collection_shapes/combined_shapes_test.cpp
        collection_shapes/identity_projection_round_trip_test.cpp)
    nucleus_enable_coverage(collection_shapes_test)
    nucleus_warnings(collection_shapes_test)
    target_link_libraries(collection_shapes_test
        PRIVATE nucleus::nucleus nucleus::argv nucleus::xml nucleus::runtime
                Catch2::Catch2WithMain)
    target_include_directories(collection_shapes_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/collection_shapes
    )
    target_compile_definitions(collection_shapes_test PRIVATE
        NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/collection_shapes/fixtures")
    add_test(NAME collection_shapes_test COMMAND collection_shapes_test)

    # System tests: end-to-end coverage of the space + stack + configuration triple.
    # Each links the real shipped modules it exercises so the linker verifies the module
    # boundaries are correct.

    # Multi-source system load: env + XML inheritance chain + argv, asserting values,
    # provenance (winning layer), cross-source last-listed-wins, and inheritance resolution.
    add_executable(system_multi_source_test system_multi_source_test.cpp)
    nucleus_enable_coverage(system_multi_source_test)
    nucleus_warnings(system_multi_source_test)
    target_link_libraries(system_multi_source_test
        PRIVATE nucleus::nucleus nucleus::env nucleus::argv nucleus::xml nucleus::runtime
                Catch2::Catch2WithMain)
    target_include_directories(system_multi_source_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME system_multi_source_test COMMAND system_multi_source_test)

    # Trident disconnection: inner-scope XML + env stack dropped; every value read back
    # after the entire source_stack, space, and arena are destroyed. Passes under ASan.
    add_executable(system_stack_buffer_drop_test system_stack_buffer_drop_test.cpp)
    nucleus_enable_coverage(system_stack_buffer_drop_test)
    nucleus_warnings(system_stack_buffer_drop_test)
    target_link_libraries(system_stack_buffer_drop_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::env
                Catch2::Catch2WithMain)
    target_include_directories(system_stack_buffer_drop_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME system_stack_buffer_drop_test COMMAND system_stack_buffer_drop_test)

    # One-space-many-stacks: one sealed space serves two different source stacks
    # and yields two independent, simultaneously-valid configurations.
    add_executable(system_one_space_many_stacks_test system_one_space_many_stacks_test.cpp)
    nucleus_enable_coverage(system_one_space_many_stacks_test)
    nucleus_warnings(system_one_space_many_stacks_test)
    target_link_libraries(system_one_space_many_stacks_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::env nucleus::runtime
                Catch2::Catch2WithMain)
    target_include_directories(system_one_space_many_stacks_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME system_one_space_many_stacks_test COMMAND system_one_space_many_stacks_test)

    # Round-trip: runtime_source + XML repeated field -> load -> emit -> reload;
    # asserts C2 == C1 by value (key set, scalars, repeated-field multiplicity).
    add_executable(system_round_trip_test
        system_round_trip_test.cpp
        system_value_round_trip_test.cpp)
    nucleus_enable_coverage(system_round_trip_test)
    nucleus_warnings(system_round_trip_test)
    target_link_libraries(system_round_trip_test
        PRIVATE nucleus::nucleus nucleus::xml nucleus::runtime nucleus::env
                Catch2::Catch2WithMain)
    target_include_directories(system_round_trip_test PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
    add_test(NAME system_round_trip_test COMMAND system_round_trip_test)

endif()
unset(nucleus_test_dir)

include(resolve/tests.cmake)
include(utility/tests.cmake)
include(diagnostics/tests.cmake)
include(registry/tests.cmake)
include(env/tests.cmake)
include(error/tests.cmake)
include(config_node/tests.cmake)
include(completion/tests.cmake)
include(capability/tests.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/example_review_tests.cmake)
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
set_tests_properties(flat_ownership_negative_compile PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE)

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
set_tests_properties(flat_ownership_entangling_ctor_negative_compile PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE)

add_executable(entangling_ctor_fixture EXCLUDE_FROM_ALL
    isolation/entangling_ctor_fixture.cpp)
target_link_libraries(entangling_ctor_fixture PRIVATE nucleus::nucleus)
target_include_directories(entangling_ctor_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

add_test(
    NAME source_concept_unsatisfied_compile
    COMMAND ${CMAKE_COMMAND} --build . --target source_concept_unsatisfied_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(source_concept_unsatisfied_compile PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE)

add_executable(source_concept_unsatisfied_fixture EXCLUDE_FROM_ALL
    isolation/source_concept_unsatisfied_fixture.cpp)
target_link_libraries(source_concept_unsatisfied_fixture PRIVATE nucleus::nucleus)
target_include_directories(source_concept_unsatisfied_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

# ---------------------------------------------------------------------------
# Reintroduced-warning proof: a fixture that deliberately stores an unread
# private field. Routing it through nucleus_warnings() puts it under the same
# centralized -Werror a developer hits locally. Rather than a bare WILL_FAIL
# (which any nonzero build exit satisfies), the compile is driven through a
# wrapper that asserts the build both fails AND fails on the exact clang-only
# diagnostic (-Wunused-private-field) that once slipped through non-fatally --
# so an unrelated compile break cannot pass the test for the wrong reason.
# Clang-gated: GCC has no such diagnostic, so there the fixture compiles clean.
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_test(
        NAME reintroduced_warning_negative_compile
        COMMAND ${CMAKE_COMMAND}
            -DNUCLEUS_BUILD_DIR=${CMAKE_BINARY_DIR}
            -DNUCLEUS_TARGET=reintroduced_warning_fixture
            -DNUCLEUS_CONFIG=$<CONFIG>
            -DNUCLEUS_EXPECT=unused-private-field
            -P ${CMAKE_SOURCE_DIR}/scripts/expect_warning_compile_failure.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(reintroduced_warning_negative_compile PROPERTIES RUN_SERIAL TRUE)

    add_executable(reintroduced_warning_fixture EXCLUDE_FROM_ALL
        isolation/reintroduced_warning_fixture.cpp)
    nucleus_warnings(reintroduced_warning_fixture)
    target_link_libraries(reintroduced_warning_fixture PRIVATE nucleus::nucleus)
    target_include_directories(reintroduced_warning_fixture PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
endif()

# ---------------------------------------------------------------------------
# NUCLEUS_WERROR top-level-only default: proves nucleus_IS_TOP_LEVEL-derived
# default resolves OFF for a nested add_subdirectory() consumer whose own
# project() call precedes nucleus's. Unconditional (not clang-gated) since the
# check is compiler-independent. A bare "configure succeeds" check would be
# insufficient -- it must also confirm the SPECIFIC cache value, not merely
# that configure didn't crash.
# ---------------------------------------------------------------------------
add_test(
    NAME nested_consumer_werror_default_off
    COMMAND ${CMAKE_COMMAND}
        -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/nested_consumer_fixture
        -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/nested_consumer_fixture_build
        -P ${CMAKE_SOURCE_DIR}/scripts/expect_nested_consumer_werror_off.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ---------------------------------------------------------------------------
# Vendoring-consumer install gate: a project that vendors nucleus and installs
# an executable of its own must not be forced to leave a transitive dependency's
# install rules enabled to satisfy an export set that dependency is not in. A
# bare "configure succeeds" check would be insufficient -- it must confirm the
# SPECIFIC resolved cache value. Linux-only: the claim is about CMake option
# resolution, and the entry costs a second nucleus configure plus a dependency
# population no other platform's suite run should pay for.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Reuse a dependency source the outer build already populated rather than
    # cloning a second copy; with none on disk the script populates its own.
    set(nucleus_fixture_deps ${CMAKE_BINARY_DIR}/_deps)
    if(FETCHCONTENT_BASE_DIR)
        set(nucleus_fixture_deps ${FETCHCONTENT_BASE_DIR})
    endif()
    set(nucleus_fixture_args)
    if(EXISTS ${nucleus_fixture_deps}/pugixml-src)
        set(nucleus_fixture_args
            -DNUCLEUS_FIXTURE_PUGIXML_SOURCE_DIR=${nucleus_fixture_deps}/pugixml-src)
    endif()
    add_test(
        NAME nested_consumer_install_default_off
        COMMAND ${CMAKE_COMMAND}
            -DNUCLEUS_FIXTURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/tests/install_consumer_fixture
            -DNUCLEUS_FIXTURE_BINARY_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_build
            -DNUCLEUS_FIXTURE_PREFIX_DIR=${CMAKE_BINARY_DIR}/install_consumer_fixture_prefix
            ${nucleus_fixture_args}
            -P ${CMAKE_SOURCE_DIR}/scripts/expect_install_off.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    # A nested configure, a dependency population and a consumer build are not a
    # unit test's budget, and a per-test TIMEOUT outranks ctest --timeout.
    set_tests_properties(nested_consumer_install_default_off PROPERTIES TIMEOUT 1200)
endif()

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
# Sanitizer trip tests: built and registered ONLY when the matching sanitizer is
# enabled. Each deliberately commits the defect its sanitizer exists to catch
# (a heap use-after-free; an unsynchronized cross-thread write), proving the
# instrumentation is wired and tripping; neither is part of a normal run.
# ---------------------------------------------------------------------------
if(NUCLEUS_BUILD_SANITIZER AND NUCLEUS_SANITIZER STREQUAL "address")
    add_executable(asan_trip_test asan_trip_test.cpp)
    target_link_libraries(asan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME asan_trip_test COMMAND asan_trip_test)
    set_tests_properties(asan_trip_test PROPERTIES
        PASS_REGULAR_EXPRESSION "heap-use-after-free|AddressSanitizer")
endif()

if(NUCLEUS_BUILD_SANITIZER AND NUCLEUS_SANITIZER STREQUAL "thread")
    add_executable(tsan_trip_test tsan_trip_test.cpp)
    target_link_libraries(tsan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME tsan_trip_test COMMAND tsan_trip_test)
    set_tests_properties(tsan_trip_test PROPERTIES
        PASS_REGULAR_EXPRESSION "data race|ThreadSanitizer")
endif()

# UBSan is present in BOTH the dedicated "undefined" flavor and the composed
# "address" flavor (-fsanitize=address,undefined), so the trip test is gated to
# both. Beyond the PASS regex (the diagnostic fired) this asserts a
# FAIL_REGULAR_EXPRESSION on the post-overflow sentinel: because UBSan is built
# non-recovering, the process must abort before the sentinel prints. Without
# -fno-sanitize-recover=undefined the sentinel would print and this test would
# fail -- that FAIL clause is what proves the vacuous-pass hole is closed.
if(NUCLEUS_BUILD_SANITIZER AND
   (NUCLEUS_SANITIZER STREQUAL "undefined" OR NUCLEUS_SANITIZER STREQUAL "address"))
    add_executable(ubsan_trip_test ubsan_trip_test.cpp)
    target_link_libraries(ubsan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME ubsan_trip_test COMMAND ubsan_trip_test)
    # abort_on_error=0 makes UBSan halt via _exit(non-zero) instead of a raising
    # SIGABRT (the Darwin default): a signal death is reported by CTest as an
    # exception that overrides PASS_REGULAR_EXPRESSION, so the regex assertions
    # below would never be consulted. halt_on_error=1 keeps it non-recovering
    # (belt-and-braces with the compile-time -fno-sanitize-recover=undefined),
    # so the process still stops at the overflow and the sentinel never prints.
    set_tests_properties(ubsan_trip_test PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=abort_on_error=0:halt_on_error=1:print_stacktrace=1"
        PASS_REGULAR_EXPRESSION "runtime error:|UndefinedBehaviorSanitizer"
        FAIL_REGULAR_EXPRESSION "unreachable-under-ubsan")
endif()

# ---------------------------------------------------------------------------
# Public-surface gate: a generated TU includes EVERY public header and compiles
# with ONLY the public include roots on the path -- no lib/*/src anywhere. This
# is the installed consumer's view of the library; a public header that reaches
# into a private one breaks this target even though every other test (which has
# lib/core/src on its include path) keeps compiling.
# ---------------------------------------------------------------------------
set(public_surface_roots
    ${CMAKE_SOURCE_DIR}/lib/core/include
    ${CMAKE_SOURCE_DIR}/lib/env/include
    ${CMAKE_SOURCE_DIR}/lib/argv/include
    ${CMAKE_SOURCE_DIR}/lib/runtime/include)
if(NUCLEUS_BUILD_SOURCE_XML)
    list(APPEND public_surface_roots ${CMAKE_SOURCE_DIR}/lib/xml/include)
endif()

set(public_surface_tu "")
foreach(root ${public_surface_roots})
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS RELATIVE ${root} ${root}/nucleus/*.h)
    list(SORT public_headers)
    foreach(header ${public_headers})
        string(APPEND public_surface_tu "#include <${header}>\n")
    endforeach()
endforeach()
string(APPEND public_surface_tu "\nint main() {}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/public_surface_test.cpp "${public_surface_tu}")

add_executable(public_surface_test ${CMAKE_CURRENT_BINARY_DIR}/public_surface_test.cpp)
target_link_libraries(public_surface_test
    PRIVATE nucleus::nucleus nucleus::env nucleus::argv nucleus::runtime)
if(NUCLEUS_BUILD_SOURCE_XML)
    target_link_libraries(public_surface_test PRIVATE nucleus::xml)
endif()
add_test(NAME public_surface_test COMMAND public_surface_test)
