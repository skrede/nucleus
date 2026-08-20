# The ordinal boundary regressions compile into the flat-emit target rather than
# one of their own: they assert the same emit contract and must fail with it.
nucleus_add_test(flat_emit_test nucleus::env nucleus::argv nucleus::runtime)
target_sources(flat_emit_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/flat_emit_ordinal_test.cpp)

# The ordinal-domain rejection proof drives argv, env and runtime sources, so it
# links the flat surfaces alongside the core.
nucleus_add_test(ordinal_domain_test nucleus::env nucleus::argv nucleus::runtime)

# The flat record grammar regressions assert the same emit contract as the ordinal
# boundaries, so they compile into the flat-emit target rather than one of their own.
target_sources(flat_emit_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/flat_emit_record_test.cpp)
