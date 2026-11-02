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
set(public_header_paths "")
foreach(root ${public_surface_roots})
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS RELATIVE ${root} ${root}/nucleus/*.h)
    list(SORT public_headers)
    list(APPEND public_header_paths ${public_headers})
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

# Each header compiles in a translation unit of its own, which the combined test above cannot show:
# there, any header listed earlier may supply an include a later one is missing.
set(self_contained_dir ${CMAKE_CURRENT_BINARY_DIR}/self_contained)
set(self_contained_sources ${self_contained_dir}/self_contained_main.cpp)
file(GENERATE OUTPUT ${self_contained_dir}/self_contained_main.cpp CONTENT "int main() {}\n")
foreach(header ${public_header_paths})
    string(REGEX REPLACE "[/.]" "_" unit ${header})
    file(GENERATE OUTPUT ${self_contained_dir}/${unit}.cpp CONTENT "#include <${header}>\n")
    list(APPEND self_contained_sources ${self_contained_dir}/${unit}.cpp)
endforeach()

add_executable(public_header_self_containment_test ${self_contained_sources})
target_link_libraries(public_header_self_containment_test
    PRIVATE nucleus::nucleus nucleus::env nucleus::argv nucleus::runtime)
if(NUCLEUS_BUILD_SOURCE_XML)
    target_link_libraries(public_header_self_containment_test PRIVATE nucleus::xml)
endif()
nucleus_warnings(public_header_self_containment_test)
add_test(NAME public_header_self_containment_test COMMAND public_header_self_containment_test)
