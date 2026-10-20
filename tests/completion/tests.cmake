nucleus_add_test(completion_test)

# Catch2 exits 4 when every selected test was skipped, which here means no bash 5.0 or later on PATH; tell
# CTest that is a skip and not a failure.
nucleus_add_test(smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(program_token_test)

nucleus_add_test(zsh_spec_test)

nucleus_add_test(completion_projection_test)

nucleus_add_test(bash_ordinal_test)

nucleus_add_test(bash_ordinal_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(zsh_ordinal_test)

nucleus_add_test(bash_insertion_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(bash_quoted_insertion_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(bash_quote_state_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(bash_cursor_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(emitter_model_test PROPERTIES SKIP_RETURN_CODE 4)
