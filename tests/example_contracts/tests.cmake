target_sources(schema_test PRIVATE ${nucleus_test_dir}/schema_contract_test.cpp)
# schema/ registers this target only when the XML source is built, so the condition to test is that
# option -- not the target's existence, which is also false when this fragment is included too early
# and would then silently compile the contract into nothing.
if(NUCLEUS_BUILD_SOURCE_XML)
    if(NOT TARGET typed_element_test)
        message(FATAL_ERROR
            "example_contracts must be included after schema registers typed_element_test")
    endif()
    target_sources(typed_element_test PRIVATE ${nucleus_test_dir}/typed_contract_test.cpp)
endif()
target_sources(pkey_tokenizer_test PRIVATE ${nucleus_test_dir}/pkey_tokenizer_contract_test.cpp)
target_sources(selector_combinator_test PRIVATE ${nucleus_test_dir}/query_contract_test.cpp)
target_sources(recognizer_of_test PRIVATE ${nucleus_test_dir}/argv_recognizer_contract_test.cpp)
target_sources(auto_gating_test PRIVATE ${nucleus_test_dir}/capability_gating_contract_test.cpp)
target_link_libraries(auto_gating_test PRIVATE nucleus::runtime)
target_sources(constraint_group_test PRIVATE ${nucleus_test_dir}/constraint_groups_contract_test.cpp)
target_sources(registration_policy_test PRIVATE ${nucleus_test_dir}/plugin_spaces_contract_test.cpp)
target_link_libraries(registration_policy_test PRIVATE nucleus::runtime)
