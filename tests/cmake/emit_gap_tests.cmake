# The ordinal boundary regressions compile into the flat-emit target rather than
# one of their own: they assert the same emit contract and must fail with it.
nucleus_add_test(flat_emit_test nucleus::env nucleus::argv nucleus::runtime)
target_sources(flat_emit_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/flat_emit_ordinal_test.cpp)
