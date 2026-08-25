set(nucleus_test_dir query)

nucleus_add_test(keyref_test LINK nucleus::runtime)
nucleus_add_test(retrieval_error_test LINK nucleus::runtime)
nucleus_add_test(selector_test LINK nucleus::runtime)
nucleus_add_test(selector_combinator_test LINK nucleus::runtime)
nucleus_add_test(selector_schema_role_test LINK nucleus::runtime)
nucleus_add_test(selector_owner_test LINK nucleus::runtime)
nucleus_add_test(selector_strain_test LINK nucleus::runtime)
nucleus_add_test(selector_lifetime_test LINK nucleus::runtime)

if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(retrieval_addressing_test LINK nucleus::runtime nucleus::xml nucleus::argv)
    nucleus_add_test(instance_addressing_test LINK nucleus::runtime nucleus::xml nucleus::argv)
endif()

unset(nucleus_test_dir)
