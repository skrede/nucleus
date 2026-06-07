# Example programs, built under -DNUCLEUS_BUILD_EXAMPLES=ON. Each is a small,
# self-contained main() focused on one concept and linked against the core
# library (and, where it needs them, the separately-linked source/tokenizer
# modules). They are demonstrations, not tests, so they are not registered with
# CTest.

# Registers an example executable linked against the core library.
function(nucleus_add_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
endfunction()

# Getting started.
nucleus_add_example(quickstart)
nucleus_add_example(schema)

# Sources: the built-in flat sources and the seam a host extends.
nucleus_add_example(argv)
nucleus_add_example(env)
nucleus_add_example(layering)
nucleus_add_example(custom_source)
nucleus_add_example(parser_concept)

# Tokens, logging, completion, diagnostics, and the registration-policy seam.
nucleus_add_example(tokens)
nucleus_add_example(logging)
nucleus_add_example(completion)
nucleus_add_example(diagnostics)
nucleus_add_example(registration_policy)

# The XML example layers an XML document under the command line, so it links the
# XML source module. That module's header lives under the internal src/ tree (the
# one place pugixml is reachable), so the example adds src/ to its include path --
# the same way the buffer-lifetime test reaches it. Built only when the XML
# module is.
if(NUCLEUS_BUILD_SOURCE_XML)
    add_executable(xml xml.cpp)
    target_link_libraries(xml PRIVATE nucleus::nucleus nucleus::source_xml)
    target_include_directories(xml PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    add_executable(strains strains.cpp)
    target_link_libraries(strains PRIVATE nucleus::nucleus nucleus::source_xml)
    target_include_directories(strains PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    add_executable(typed typed.cpp)
    target_link_libraries(typed PRIVATE nucleus::nucleus nucleus::source_xml)
    target_include_directories(typed PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
endif()
