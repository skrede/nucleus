# Consumer integration test: builds against an INSTALLED nucleus package via
# find_package, never against the source tree. Proves the install/export
# surface (headers, targets, package config) actually works for an adopter.
cmake_minimum_required(VERSION 3.21)
project(nucleus_integration_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nucleus REQUIRED CONFIG)

add_executable(integration_test integration_test.cpp)
target_link_libraries(integration_test PRIVATE nucleus::nucleus nucleus::runtime)
