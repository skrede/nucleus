cmake_minimum_required(VERSION 3.24)
project(nucleus_nested_consumer_fixture CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This fixture's own project() call runs first, then nucleus is pulled in as a
# nested, non-top-level project -- the exact FetchContent/add_subdirectory
# shape a real consumer uses. Configure-time evaluation of nucleus's own
# option() declarations is enough to prove nucleus_IS_TOP_LEVEL (and therefore
# NUCLEUS_WERROR's default) resolves to OFF/FALSE here; no target is needed.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. nucleus_build)
