# Vendoring-consumer install-gate assertion.
#
# Configures the install_consumer_fixture (a standalone project whose own
# project() call precedes nucleus's, add_subdirectory()-ing the real nucleus tree
# and installing an executable of its own) with the XML dependency's install rules
# off, and asserts the resulting cache resolves NUCLEUS_INSTALL to OFF. A bare
# "did configure succeed" check would be insufficient on its own: it would not
# catch NUCLEUS_INSTALL silently defaulting back to ON for a vendoring consumer,
# which is the exact regression this test exists to prevent.
#
# find_package is disabled for pugixml alone so the dependency is populated from
# source instead of resolved. A discovered package is an imported target that
# never evaluates PUGIXML_INSTALL, so on a machine carrying a system pugixml the
# whole check would pass while measuring nothing.
#
# Invoke: cmake -DNUCLEUS_FIXTURE_SOURCE_DIR=<dir> -DNUCLEUS_FIXTURE_BINARY_DIR=<dir>
#               [-DNUCLEUS_FIXTURE_PREFIX_DIR=<dir>]
#               [-DNUCLEUS_FIXTURE_PUGIXML_SOURCE_DIR=<dir>]
#               -P scripts/expect_install_off.cmake

cmake_minimum_required(VERSION 3.25)

foreach(required NUCLEUS_FIXTURE_SOURCE_DIR NUCLEUS_FIXTURE_BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

set(configure_args -DPUGIXML_INSTALL=OFF -DCMAKE_DISABLE_FIND_PACKAGE_pugixml=ON)
if(NUCLEUS_FIXTURE_PREFIX_DIR)
    list(APPEND configure_args -DCMAKE_INSTALL_PREFIX=${NUCLEUS_FIXTURE_PREFIX_DIR})
endif()
# An already-populated source skips the clone and still takes the FetchContent
# path, so the population assertion below holds either way.
if(NUCLEUS_FIXTURE_PUGIXML_SOURCE_DIR)
    list(APPEND configure_args
        -DFETCHCONTENT_SOURCE_DIR_PUGIXML=${NUCLEUS_FIXTURE_PUGIXML_SOURCE_DIR})
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${NUCLEUS_FIXTURE_SOURCE_DIR} -B ${NUCLEUS_FIXTURE_BINARY_DIR}
            ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE  configure_stderr)

# Read the cache before reporting a failed configure: a gate that resolved ON is
# the explanation for the export error such a configure dies on, and naming the
# resolved value is more use than naming the generator's complaint about it.
set(install_lines)
if(EXISTS "${NUCLEUS_FIXTURE_BINARY_DIR}/CMakeCache.txt")
    file(STRINGS "${NUCLEUS_FIXTURE_BINARY_DIR}/CMakeCache.txt" install_lines
        REGEX "^NUCLEUS_INSTALL:BOOL=")
endif()

list(LENGTH install_lines install_line_count)
if(install_line_count EQUAL 1)
    list(GET install_lines 0 install_line)
    if(NOT install_line STREQUAL "NUCLEUS_INSTALL:BOOL=OFF")
        message(FATAL_ERROR
            "NUCLEUS_INSTALL did not resolve OFF for the vendoring consumer: found '${install_line}'")
    endif()
endif()

if(NOT configure_result EQUAL 0)
    message("${configure_stdout}${configure_stderr}")
    message(FATAL_ERROR
        "the vendoring consumer failed to configure with the dependency's install rules off")
endif()

if(NOT install_line_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one NUCLEUS_INSTALL:BOOL= cache entry, found ${install_line_count}")
endif()

if(NOT IS_DIRECTORY "${NUCLEUS_FIXTURE_BINARY_DIR}/_deps/pugixml-build")
    message(FATAL_ERROR
        "pugixml was resolved rather than populated, so PUGIXML_INSTALL was never evaluated "
        "and this check proves nothing about the dependency's install rules")
endif()

set(prefix_note "")
if(NUCLEUS_FIXTURE_PREFIX_DIR)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${NUCLEUS_FIXTURE_BINARY_DIR}
                --target install_consumer --parallel
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE  build_stderr)
    if(NOT build_result EQUAL 0)
        message("${build_stdout}${build_stderr}")
        message(FATAL_ERROR "the vendoring consumer failed to build its own executable")
    endif()

    file(REMOVE_RECURSE "${NUCLEUS_FIXTURE_PREFIX_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --install ${NUCLEUS_FIXTURE_BINARY_DIR}
                --prefix ${NUCLEUS_FIXTURE_PREFIX_DIR}
        RESULT_VARIABLE install_result
        OUTPUT_VARIABLE install_stdout
        ERROR_VARIABLE  install_stderr)
    if(NOT install_result EQUAL 0)
        message("${install_stdout}${install_stderr}")
        message(FATAL_ERROR "the vendoring consumer failed to install its own executable")
    endif()

    file(GLOB_RECURSE installed_files RELATIVE "${NUCLEUS_FIXTURE_PREFIX_DIR}"
        "${NUCLEUS_FIXTURE_PREFIX_DIR}/*")
    set(leaked)
    foreach(entry IN LISTS installed_files)
        if(entry MATCHES "nucleus|pugixml")
            list(APPEND leaked "${entry}")
        endif()
    endforeach()
    if(leaked)
        list(JOIN leaked ", " leaked_text)
        message(FATAL_ERROR
            "the vendoring consumer's prefix carries dependency files it never asked for: ${leaked_text}")
    endif()
    set(prefix_note ", and its own install prefix carries no nucleus or pugixml files")
endif()

message(STATUS
    "install gate check: NUCLEUS_INSTALL resolves OFF for a vendoring consumer, pugixml was "
    "populated from source with its install rules off${prefix_note}")
