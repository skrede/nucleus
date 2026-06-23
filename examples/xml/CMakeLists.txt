# XML format: layering a document under the command line, persistence, template
# emission, and a multi-format round trip. The whole group needs the xml module;
# its public adapter headers under nucleus/xml/ arrive via that link.
if(NUCLEUS_BUILD_SOURCE_XML)
    nucleus_add_example(emit_template nucleus::xml)
    nucleus_add_example(xml nucleus::xml nucleus::argv)
    nucleus_add_example(xml_persist nucleus::xml)
    nucleus_add_example(round_trip nucleus::xml nucleus::runtime nucleus::env nucleus::argv)
endif()
