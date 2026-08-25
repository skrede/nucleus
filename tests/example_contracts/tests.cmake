set(nucleus_test_dir example_contracts)

target_sources(schema_test PRIVATE ${nucleus_test_dir}/schema_contract_test.cpp)
# The guard tolerates a configuration that never defines the target, and it is also the one
# statement here that fails quietly: included before the folder that registers the target it would
# evaluate false, this source would compile into nothing, and only the assertion total would move.
if(TARGET typed_element_test)
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

unset(nucleus_test_dir)
