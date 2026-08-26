nucleus_add_test(expected_test)
nucleus_add_test(code_interop_test)

if(NUCLEUS_BUILD_SOURCE_XML)
    # Every public failure channel must carry the code naming its pipeline stage; linking the XML
    # and runtime modules is what drives each of those paths for real rather than in isolation.
    nucleus_add_test(code_test LINK nucleus::xml nucleus::runtime)
endif()
