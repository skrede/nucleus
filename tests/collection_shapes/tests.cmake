set(nucleus_test_dir collection_shapes)

# The folder include entry is what lets the nineteen sources reach collection_shapes.h by its
# bare name; the fixture documents the define points at live beside them.
nucleus_add_test(collection_shapes_test
    SOURCES collection_shapes_test.cpp
            addressing_test.cpp
            keyed_ordinal_test.cpp
            keyed_merge_stability_test.cpp
            keyed_duplicate_test.cpp
            keyed_mixed_source_test.cpp
            error_channel_test.cpp
            keyref_repeated_namespace_test.cpp
            strain_displacement_test.cpp
            strain_compaction_test.cpp
            strain_compaction_boundary_test.cpp
            strain_ordering_test.cpp
            transparent_root_test.cpp
            instance_presence_test.cpp
            violation_report_test.cpp
            unique_scope_test.cpp
            identity_pool_scope_test.cpp
            combined_shapes_test.cpp
            identity_projection_round_trip_test.cpp
    LINK nucleus::argv nucleus::xml nucleus::runtime
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/collection_shapes
    DEFINES NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/collection_shapes/fixtures")

unset(nucleus_test_dir)
