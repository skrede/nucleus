# The character-grammar regressions assert the same emit contract as the rest of the xml surface, so
# they compile into that target; the include entry reaches the grammar header in the private tree.
nucleus_add_test(xml_emit_contract_test
    SOURCES emit_contract_test.cpp grammar_contract_test.cpp grammar_surface_test.cpp
    LINK nucleus::xml
    INCLUDE ${CMAKE_SOURCE_DIR}/lib/xml/src)

nucleus_add_test(template_test LINK nucleus::xml)

# Proves the copy-out severs every view from the document arena before the arena is dropped, which
# is why this test links the xml module (and pugixml transitively) that core never links.
nucleus_add_test(buffer_lifetime_test LINK nucleus::xml)

nucleus_add_test(robustness_test LINK nucleus::xml)
nucleus_add_test(keyed_projection_test LINK nucleus::xml)
nucleus_add_test(keyed_selection_test LINK nucleus::xml)
nucleus_add_test(identity_contract_test LINK nucleus::xml)
nucleus_add_test(location_token_wiring_test LINK nucleus::xml)

nucleus_add_test(xml_persist_test
    SOURCES persist_test.cpp persist_render_test.cpp persist_value_test.cpp
    LINK nucleus::xml)

nucleus_add_test(xml_repeated_container_test
    SOURCES repeated_container_test.cpp repeated_round_trip_test.cpp
            repeated_contract_test.cpp repeated_value_test.cpp
    LINK nucleus::xml nucleus::runtime)
