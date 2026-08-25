# The generated translation unit includes every public header and compiles with only the public
# include roots on the path -- the installed consumer's view. A public header that reaches into a
# private one breaks this target alone; every other test has the core's private tree on its path.
set(public_surface_roots
    ${CMAKE_SOURCE_DIR}/lib/core/include
    ${CMAKE_SOURCE_DIR}/lib/env/include
    ${CMAKE_SOURCE_DIR}/lib/argv/include
    ${CMAKE_SOURCE_DIR}/lib/runtime/include)
if(NUCLEUS_BUILD_SOURCE_XML)
    list(APPEND public_surface_roots ${CMAKE_SOURCE_DIR}/lib/xml/include)
endif()

set(public_surface_tu "")
foreach(root ${public_surface_roots})
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS RELATIVE ${root} ${root}/nucleus/*.h)
    list(SORT public_headers)
    foreach(header ${public_headers})
        string(APPEND public_surface_tu "#include <${header}>\n")
    endforeach()
endforeach()
string(APPEND public_surface_tu "\nint main() {}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/public_surface_test.cpp "${public_surface_tu}")

add_executable(public_surface_test ${CMAKE_CURRENT_BINARY_DIR}/public_surface_test.cpp)
target_link_libraries(public_surface_test
    PRIVATE nucleus::nucleus nucleus::env nucleus::argv nucleus::runtime)
if(NUCLEUS_BUILD_SOURCE_XML)
    target_link_libraries(public_surface_test PRIVATE nucleus::xml)
endif()
add_test(NAME public_surface_test COMMAND public_surface_test)
