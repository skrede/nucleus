# The ordinal-boundary and flat-record regressions assert the same emit contract as the flat-emit
# surface and must fail with it, so they compile into that target rather than taking their own.
nucleus_add_test(flat_emit_test
    SOURCES flat_emit_test.cpp flat_emit_ordinal_test.cpp flat_emit_record_test.cpp
    LINK nucleus::env nucleus::argv nucleus::runtime)
nucleus_add_test(flat_emit_round_trip_test LINK nucleus::env nucleus::argv nucleus::runtime)
nucleus_add_test(emitter_contract_test LINK nucleus::env nucleus::argv)

if(NUCLEUS_BUILD_SOURCE_XML)
    # The seam proof builds all three emitters from one space, so it links all three modules.
    nucleus_add_test(seam_test LINK nucleus::xml nucleus::env nucleus::argv)
endif()
