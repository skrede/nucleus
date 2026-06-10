# Consumer integration test: builds against an INSTALLED nucleus package via
# find_package, never against the source tree. Proves the install/export
# surface (headers, targets, package config) actually works for an adopter.
cmake_minimum_required(VERSION 3.21)
project(nucleus_integration_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# When the installed package was built with the xml module, exercising it here
# proves the riskiest export paths: the nucleus::xml target, its find_dependency
# on pugixml, and the $<LINK_ONLY:pugixml::pugixml> reference in the static
# archive's link interface.
option(NUCLEUS_INTEGRATION_WITH_XML "Also consume the installed xml module" OFF)

find_package(nucleus REQUIRED CONFIG)

add_executable(integration_test integration_test.cpp)
target_link_libraries(integration_test PRIVATE nucleus::nucleus nucleus::runtime)
if(NUCLEUS_INTEGRATION_WITH_XML)
    target_link_libraries(integration_test PRIVATE nucleus::xml)
    target_compile_definitions(integration_test PRIVATE NUCLEUS_INTEGRATION_WITH_XML)
endif()
