# Schema and validation: element kinds, typed conversion, strain selection, and
# constraint/identity composition.
nucleus_add_example(schema nucleus::runtime)
nucleus_add_example(constraint_groups nucleus::runtime)
nucleus_add_example(keyed_composition nucleus::runtime)

# typed and strains load from an XML document, so they need the xml module.
if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_example(typed nucleus::xml)
    nucleus_add_example(strains nucleus::xml)
endif()
