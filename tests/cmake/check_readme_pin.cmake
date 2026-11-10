cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS NUCLEUS_README NUCLEUS_PROJECT_FILE NUCLEUS_PIN_DIR NUCLEUS_PIN_REQUIRE_PUBLISHED)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be set")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/readme_extract.cmake")

find_program(git_exe NAMES git REQUIRED)
vendoring_pin("${NUCLEUS_README}" repository tag)
project_version("${NUCLEUS_PROJECT_FILE}" version)

function(resolve_commit sha)
    set(clone "${NUCLEUS_PIN_DIR}/remote.git")
    file(REMOVE_RECURSE "${clone}")
    file(MAKE_DIRECTORY "${NUCLEUS_PIN_DIR}")
    # A partial clone would lazily fetch a commit known only to the fork network, so this one is full.
    execute_process(COMMAND "${git_exe}" clone --bare --quiet "${repository}" "${clone}"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "could not clone ${repository} to resolve the README pin\n${stderr}")
    endif()
    # A commit present but contained in no published ref still exits 0, so the verdict is the output.
    execute_process(COMMAND "${git_exe}" -C "${clone}" for-each-ref "--format=%(refname)" --contains "${sha}"
        refs/heads refs/tags
        OUTPUT_VARIABLE refs ERROR_VARIABLE stderr OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(refs STREQUAL "")
        message(FATAL_ERROR "${NUCLEUS_README} pins ${sha}, which no branch or tag published at ${repository} contains\n${stderr}")
    endif()
    string(REPLACE "\n" ", " refs "${refs}")
    message(STATUS "readme pin: ${sha} is published at ${repository} on ${refs}")
endfunction()

function(resolve_tag name)
    execute_process(COMMAND "${git_exe}" ls-remote --tags "${repository}" "refs/tags/${name}"
        RESULT_VARIABLE status OUTPUT_VARIABLE published ERROR_VARIABLE stderr OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "could not query ${repository} for the README pin ${name}\n${stderr}")
    endif()
    if(NOT published STREQUAL "")
        message(STATUS "readme pin: ${name} is published at ${repository}")
    elseif(NOT name STREQUAL "v${version}")
        message(FATAL_ERROR "${NUCLEUS_README} pins ${name}, which ${repository} has not published "
            "and which is not the release this tree declares (v${version})")
    elseif(NUCLEUS_PIN_REQUIRE_PUBLISHED)
        # On a release-tag run the pinned tag should already exist, so a pending pass would hide a lookup that finds none.
        message(FATAL_ERROR "${NUCLEUS_README} pins ${name}, the release this tree declares, but ${repository} "
            "has not published it, and a release-tag run passes only a published tag")
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E echo
            "::notice title=README pin::pending release tag v${version}: ${NUCLEUS_README} pins the release this tree declares, which ${repository} has not published yet")
    endif()
endfunction()

string(LENGTH "${tag}" tag_length)
if(tag_length EQUAL 40 AND tag MATCHES "^[0-9a-f]+$")
    resolve_commit("${tag}")
elseif(tag MATCHES "^v")
    resolve_tag("${tag}")
else()
    message(FATAL_ERROR "${NUCLEUS_README} pins ${tag}, which is neither a full commit SHA nor a release tag, "
        "so the tree it names can move")
endif()
