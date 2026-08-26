nucleus_add_test(facade_test LINK nucleus::env)
nucleus_add_test(schema_test LINK nucleus::runtime)
nucleus_add_test(registration_policy_test)

# The name keeps its facade prefix: shedding it would produce completion_test, which the
# completion folder's own test already registers, and CMake target names and CTest names are
# each a single global namespace.
nucleus_add_test(facade_completion_test)
