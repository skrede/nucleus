set(nucleus_test_dir schema)

nucleus_add_test(registry_test)
nucleus_add_test(enforcer_test)
nucleus_add_test(cli_bijection_test LINK nucleus::argv)
nucleus_add_test(constraint_group_test LINK nucleus::runtime)
nucleus_add_test(identity_group_test LINK nucleus::runtime)
nucleus_add_test(keyed_composition_test LINK nucleus::runtime nucleus::argv nucleus::env)
nucleus_add_test(converter_registry_test LINK nucleus::env)
nucleus_add_test(converter_edge_cases_test)

# The definition forces the strtof/strtod fallback so that path is exercised on every platform,
# not only where the standard library lacks the fast conversion.
# Catch2 exits 4 when every test skips, which happens here on a host with no comma-decimal locale.
nucleus_add_test(converter_locale_test
    DEFINES NUCLEUS_FORCE_FP_FROM_CHARS_FALLBACK
    PROPERTIES SKIP_RETURN_CODE 4)

if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(repeated_element_test
        LINK nucleus::xml nucleus::runtime nucleus::env)
    nucleus_add_test(typed_element_test LINK nucleus::xml)
    nucleus_add_test(typed_shape_test LINK nucleus::xml nucleus::env)
endif()

unset(nucleus_test_dir)
