set(nucleus_test_dir utility)

nucleus_add_test(version_test
    DEFINES NUCLEUS_CMAKE_PROJECT_VERSION="${nucleus_VERSION}")
nucleus_add_test(type_info_test)
nucleus_add_test(escaped_text_test)

unset(nucleus_test_dir)
