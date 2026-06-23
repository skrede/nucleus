# Identity and references: pkey-as-data, keyref dereference, tree references, and
# the programmatic query API.
nucleus_add_example(keyref nucleus::runtime)
nucleus_add_example(tree_references nucleus::runtime)
nucleus_add_example(query nucleus::runtime)

# pkey_identity loads from an XML document, so it needs the xml module.
if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_example(pkey_identity nucleus::xml)
endif()
