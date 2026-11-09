cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS NUCLEUS_README NUCLEUS_CHANGELOG NUCLEUS_PROJECT_FILE NUCLEUS_CONSUMER_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be set")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/readme_extract.cmake")

find_package_call("${NUCLEUS_README}" readme_call)
find_package_call("${NUCLEUS_CONSUMER_FILE}" consumer_call)
if(NOT readme_call STREQUAL consumer_call)
    message(FATAL_ERROR "${NUCLEUS_README}: its find_package call differs from the one the integration consumer configures\n"
        "README:   ${readme_call}\nconsumer: ${consumer_call}")
endif()

project_version("${NUCLEUS_PROJECT_FILE}" version)

# Only the newest heading's version is mechanical; whether the notes are complete is a review question.
file(STRINGS "${NUCLEUS_CHANGELOG}" headings REGEX "^## ")
if(headings STREQUAL "")
    message(FATAL_ERROR "${NUCLEUS_CHANGELOG}: expected a release heading, found none")
endif()
list(GET headings 0 heading)
string(REGEX MATCH "^## ([^ ]*)" heading "${heading}")
if(NOT CMAKE_MATCH_1 STREQUAL version)
    message(FATAL_ERROR "${NUCLEUS_CHANGELOG}: the first release heading names ${CMAKE_MATCH_1}, but the project declares ${version}")
endif()

vendoring_pin("${NUCLEUS_README}" repository tag)
if(tag MATCHES "^v" AND NOT tag STREQUAL "v${version}")
    message(FATAL_ERROR "${NUCLEUS_README}: the vendoring pin names ${tag}, but the project declares ${version}; "
        "a release-tag pin must name the release this tree builds")
endif()

message(STATUS "readme claims: ${readme_call} is the call the integration consumer configures, "
    "the release notes open on ${version} as the project declares, and the vendoring pin is ${tag}")
