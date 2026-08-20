# The XML character-grammar regressions assert the same emit contract as the rest
# of the XML surface, so they compile into the emit-contract target rather than
# one of their own. The production matrix reaches the grammar header directly,
# which lives in the module's private tree.
nucleus_add_test(xml_emit_contract_test nucleus::xml)
target_sources(xml_emit_contract_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/xml_grammar_contract_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/xml_grammar_surface_test.cpp)
target_include_directories(xml_emit_contract_test
    PRIVATE ${CMAKE_SOURCE_DIR}/lib/xml/src)
