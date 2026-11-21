nucleus_add_test(facade_test LINK nucleus::env)
nucleus_add_test(schema_test
    SOURCES schema_test.cpp schema_resolve_test.cpp
    LINK nucleus::runtime)
nucleus_add_test(registration_policy_test
    SOURCES registration_policy_test.cpp registration_policy_document_paths_test.cpp)

# The name keeps its facade prefix: shedding it would produce completion_test, which the
# completion folder's own test already registers, and CMake target names and CTest names are
# each a single global namespace.
nucleus_add_test(facade_completion_test SOURCES facade_completion_test.cpp facade_help_test.cpp)
nucleus_add_test(help_text_test)
