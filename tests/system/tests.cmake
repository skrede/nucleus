set(nucleus_test_dir system)

# Each registration links the real shipped modules its path exercises, so the linker verifies
# the module boundaries instead of a test-only stand-in.

# Proves provenance -- which layer won -- and cross-source last-listed-wins over env, an XML
# inheritance chain and argv.
nucleus_add_test(multi_source_test
    LINK nucleus::env nucleus::argv nucleus::xml nucleus::runtime)

# Proves every value still reads back after the whole source stack, space and arena are
# destroyed, which is what makes the use-after-free guard real under the sanitizer.
nucleus_add_test(stack_buffer_drop_test LINK nucleus::xml nucleus::env)

# Proves one sealed space serves two independent source stacks, yielding two simultaneously
# valid configurations.
nucleus_add_test(one_space_many_stacks_test
    LINK nucleus::xml nucleus::env nucleus::runtime)

# Proves the reloaded configuration equals the original by value -- key set, scalars and
# repeated-field multiplicity -- after load, emit and reload. Two sources, one target, so the
# helper's default single-source path does not apply.
nucleus_add_test(system_round_trip_test
    SOURCES round_trip_test.cpp value_round_trip_test.cpp
    LINK nucleus::xml nucleus::runtime nucleus::env)

nucleus_add_test(integration_shape_test LINK nucleus::xml nucleus::env)

unset(nucleus_test_dir)
