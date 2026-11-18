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

nucleus_add_test(bash_admitted_values_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(bash_quoted_insertion_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(bash_quote_state_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(bash_cursor_smoke_test PROPERTIES SKIP_RETURN_CODE 4)

nucleus_add_test(emitter_model_test PROPERTIES SKIP_RETURN_CODE 4)

add_custom_target(zsh_smoke_tests)

# Catch2 exits 4 when every case skipped, which here means no zsh with zsh/zpty was reachable; tell CTest
# that is a skip and not a failure.
function(nucleus_add_zsh_smoke_test name)
    if(NOT name MATCHES "^zsh_.*_smoke_test$")
        message(FATAL_ERROR "${name} is not named zsh_*_smoke_test, the pattern the zsh ran-check selects tests by")
    endif()
    nucleus_add_test(${name}
        DEFINES NUCLEUS_ZSH_DRIVER="${CMAKE_CURRENT_SOURCE_DIR}/completion/zsh_drive.zsh"
        PROPERTIES SKIP_RETURN_CODE 4)
    add_dependencies(zsh_smoke_tests ${name})
endfunction()

nucleus_add_zsh_smoke_test(zsh_install_smoke_test)

nucleus_add_zsh_smoke_test(zsh_program_name_smoke_test)

nucleus_add_zsh_smoke_test(zsh_compsys_name_smoke_test)

nucleus_add_zsh_smoke_test(zsh_ordinal_smoke_test)

nucleus_add_zsh_smoke_test(zsh_quoted_flag_smoke_test)

nucleus_add_zsh_smoke_test(zsh_shell_syntax_delimiter_smoke_test)

nucleus_add_zsh_smoke_test(zsh_value_smoke_test)

nucleus_add_zsh_smoke_test(zsh_spec_smoke_test)

nucleus_add_zsh_smoke_test(zsh_hand_built_smoke_test)

nucleus_add_zsh_smoke_test(zsh_control_bytes_smoke_test)

nucleus_add_zsh_smoke_test(zsh_listing_smoke_test)

# CI sources these scripts under bash versions the generated script must stay silent on. Only a container
# runs that bash, so this is a plain executable rather than a CTest test.
add_executable(bash_fixture_writer ${CMAKE_CURRENT_SOURCE_DIR}/completion/bash_fixture_writer.cpp)
nucleus_warnings(bash_fixture_writer)
target_compile_features(bash_fixture_writer PRIVATE cxx_std_20)
target_include_directories(bash_fixture_writer PRIVATE ${CMAKE_SOURCE_DIR}/lib/core/src)
target_link_libraries(bash_fixture_writer PRIVATE nucleus::core)
