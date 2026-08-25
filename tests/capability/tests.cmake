set(nucleus_test_dir capability)

nucleus_add_test(gating_test LINK nucleus::env)
nucleus_add_test(requirements_test)
nucleus_add_test(auto_gating_test LINK nucleus::env)

unset(nucleus_test_dir)
