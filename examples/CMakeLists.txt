# Example programs, built under -DNUCLEUS_BUILD_EXAMPLES=ON. Each is a small,
# self-contained main() focused on one concept and linked against the core
# library (and, where it needs them, the separately-linked source/tokenizer
# modules). They are demonstrations, not tests, so they are not registered with
# CTest.

# Registers an example executable linked against the core library. Additional
# link targets (e.g. nucleus::env, nucleus::argv) are passed as extra arguments.
function(nucleus_add_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus ${ARGN})
    nucleus_warnings(${name})
endfunction()

# Getting started.
nucleus_add_example(quickstart nucleus::argv)
nucleus_add_example(schema nucleus::env)

# Sources: the built-in flat sources and the seam a host extends.
nucleus_add_example(argv nucleus::argv)
nucleus_add_example(env nucleus::env)
nucleus_add_example(layering nucleus::env nucleus::argv)
nucleus_add_example(custom_source)
nucleus_add_example(parser_concept)

# New surface: explicit source_stack composition, reusable space, and argv recognizer.
nucleus_add_example(source_stack nucleus::env nucleus::argv nucleus::runtime)
nucleus_add_example(reusable_space nucleus::runtime)
nucleus_add_example(argv_recognizer nucleus::argv)
nucleus_add_example(argv_delimiter nucleus::argv)

# Tokens, logging, completion, diagnostics, and the registration-policy seam.
nucleus_add_example(tokens nucleus::env)
nucleus_add_example(logging nucleus::argv)
nucleus_add_example(completion)
nucleus_add_example(diagnostics)
nucleus_add_example(registration_policy)
nucleus_add_example(capability_gating nucleus::env)

# The XML example layers an XML document under the command line, so it links the
# xml module (nucleus::xml). Its public adapter headers under nucleus/xml/
# arrive via that link; pugixml stays private inside lib/xml. Built only when the
# XML module is.
if(NUCLEUS_BUILD_SOURCE_XML)
    # Template emission lives in the xml module (nucleus::xml::emit_template); the
    # example links it and its header arrives via the nucleus::xml link.
    add_executable(emit_template emit_template.cpp)
    target_link_libraries(emit_template PRIVATE nucleus::nucleus nucleus::xml)
    nucleus_warnings(emit_template)

    add_executable(xml xml.cpp)
    target_link_libraries(xml PRIVATE nucleus::nucleus nucleus::xml nucleus::argv)
    nucleus_warnings(xml)

    add_executable(strains strains.cpp)
    target_link_libraries(strains PRIVATE nucleus::nucleus nucleus::xml)
    nucleus_warnings(strains)

    add_executable(typed typed.cpp)
    target_link_libraries(typed PRIVATE nucleus::nucleus nucleus::xml)
    nucleus_warnings(typed)

    add_executable(xml_persist xml_persist.cpp)
    target_link_libraries(xml_persist PRIVATE nucleus::nucleus nucleus::xml)
    nucleus_warnings(xml_persist)

    # round_trip renders one resolved configuration through xml/env/args, so it links
    # the xml, env, and args modules (all three emitters); the runtime module supplies
    # the programmatic source. Headers arrive via the respective links.
    add_executable(round_trip round_trip.cpp)
    target_link_libraries(round_trip
        PRIVATE nucleus::nucleus nucleus::xml nucleus::runtime nucleus::env nucleus::argv)
endif()
