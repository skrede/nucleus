cmake_minimum_required(VERSION 3.25)
project(nucleus_install_consumer_fixture CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This fixture's own project() call runs first, so nucleus arrives as a nested,
# non-top-level project and NUCLEUS_INSTALL's default resolves OFF. The installed
# target is an executable deliberately: a consumer whose own export set holds a
# library linking nucleus hits a structurally identical export error one level up,
# which nucleus's install gate cannot fix.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. nucleus_build)

add_executable(install_consumer consumer.cpp)
target_link_libraries(install_consumer PRIVATE nucleus::nucleus nucleus::runtime)
install(TARGETS install_consumer RUNTIME DESTINATION bin)
