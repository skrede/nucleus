nucleus_add_test(completion_test)

# Catch2 exits 4 when every selected test was skipped, which here means no bash on PATH; tell
# CTest that is a skip and not a failure.
nucleus_add_test(smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(program_token_test)

nucleus_add_test(zsh_spec_test)

nucleus_add_test(completion_projection_test)

nucleus_add_test(bash_ordinal_test)

nucleus_add_test(bash_ordinal_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(zsh_ordinal_test)

nucleus_add_test(bash_insertion_smoke_test PROPERTIES SKIP_RETURN_CODE 4)
