nucleus_add_test(multispace_argv_test LINK nucleus::argv)
nucleus_add_test(recognizer_of_test LINK nucleus::argv)

if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(source_test LINK nucleus::argv nucleus::xml nucleus::runtime)
endif()
