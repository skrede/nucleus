set(nucleus_test_dir resolve)

nucleus_add_test(resolution_test nucleus::env nucleus::argv nucleus::runtime)

if(NUCLEUS_BUILD_SOURCE_XML)
    # Drives a full facade resolve with an XML document source in the precedence stack, then
    # drops every buffer and reads every value back, so the copy-out has to have severed every
    # view from the document arena.
    nucleus_add_test(buffer_drop_test
        SOURCES buffer_drop_test.cpp
        LINK nucleus::xml nucleus::env)

    # Cases are discovered by a directory scan under the fixture root, so a new topology is a
    # data file with no source or build edit.
    nucleus_add_test(inheritance_golden_test
        LINK nucleus::xml
        DEFINES NUCLEUS_GOLDEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resolve/fixtures")
endif()

unset(nucleus_test_dir)
