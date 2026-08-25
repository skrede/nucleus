set(nucleus_test_dir config_node)

nucleus_add_test(parent_test LINK nucleus::runtime)

if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(config_node_test LINK nucleus::xml nucleus::runtime)
endif()

unset(nucleus_test_dir)
