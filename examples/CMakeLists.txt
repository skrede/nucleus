# Example programs, built under -DNUCLEUS_BUILD_EXAMPLES=ON. Each is a small,
# self-contained main() that links the core library (and, where it needs them,
# the separately-linked source/tokenizer modules). They are demonstrations, not
# tests, so they are not registered with CTest.

# Registers an example executable linked against the core library.
function(nucleus_add_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
endfunction()

nucleus_add_example(tokens)
nucleus_add_example(logging)
nucleus_add_example(completion)

# The quickstart layers an XML document under the command line, so it links the
# XML source module. The module's header lives under the internal src/ tree (it
# is the one place pugixml is reachable), so the example adds src/ to its include
# path -- the same way the buffer-lifetime test reaches it. Built only when the
# XML module is.
if(NUCLEUS_BUILD_SOURCE_XML)
    add_executable(quickstart quickstart.cpp)
    target_link_libraries(quickstart PRIVATE nucleus::nucleus nucleus::source_xml)
    target_include_directories(quickstart PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
endif()
