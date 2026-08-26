nucleus_add_test(source_seam_test)
nucleus_add_test(source_handle_test)
nucleus_add_test(source_concept_test)
nucleus_add_test(source_stack_test)
nucleus_add_test(discovery_test)
nucleus_add_test(path_text_test)

# The inherit declaration is only observable through a source that carries one, so the chain is
# exercised against a real XML document layered over env.
if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_test(inherit_chain_test LINK nucleus::xml nucleus::env)
endif()
