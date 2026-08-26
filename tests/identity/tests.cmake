nucleus_add_test(identity_test)

if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(identity_envelope_test
        SOURCES envelope_test.cpp envelope_emit_test.cpp envelope_inheritance_test.cpp
        LINK nucleus::xml nucleus::argv)

    nucleus_add_test(pkey_identity_test
        SOURCES pkey_identity_test.cpp pkey_identity_read_test.cpp
        LINK nucleus::xml nucleus::runtime)
endif()
