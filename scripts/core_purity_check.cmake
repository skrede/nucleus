# Core-purity check.
#
# The core must be domain-neutral and format-agnostic: no XML / pugixml coupling
# and no host vocabulary leaking into it. Parser dependencies live only inside
# their own source modules (e.g. a future src/nucleus/xml/), never in core. This
# script greps the public headers and core implementation for forbidden symbols
# and fails (nonzero exit) on any hit. Run as a CTest gate.
#
# Invoke: cmake -DNUCLEUS_ROOT=<repo> -P scripts/core_purity_check.cmake

if(NOT DEFINED NUCLEUS_ROOT)
    set(NUCLEUS_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

# Forbidden vocabulary. Case-insensitive substrings that must not appear in core:
# format-specific (xml/pugi) and host-specific (vagus/node-vocabulary) symbols.
set(forbidden "pugixml" "pugi::" "vagus" "<vagus>" "<node>")

# Directories that are allowed to mention parser/host vocabulary because they ARE
# the quarantined module (none exist yet; listed so the gate stays correct once
# the xml source module lands).
set(quarantined_dirs "${NUCLEUS_ROOT}/src/nucleus/xml")

file(GLOB_RECURSE core_files
    "${NUCLEUS_ROOT}/include/nucleus/*.h"
    "${NUCLEUS_ROOT}/src/nucleus/*.h"
    "${NUCLEUS_ROOT}/src/nucleus/*.cpp"
)

set(violations "")

foreach(file ${core_files})
    set(is_quarantined FALSE)
    foreach(dir ${quarantined_dirs})
        if(file MATCHES "^${dir}/")
            set(is_quarantined TRUE)
        endif()
    endforeach()
    if(is_quarantined)
        continue()
    endif()

    file(READ "${file}" contents)
    string(TOLOWER "${contents}" lowered)
    foreach(symbol ${forbidden})
        string(TOLOWER "${symbol}" needle)
        string(FIND "${lowered}" "${needle}" position)
        if(NOT position EQUAL -1)
            list(APPEND violations "${file}: contains forbidden symbol '${symbol}'")
        endif()
    endforeach()
endforeach()

list(LENGTH violations violation_count)
if(violation_count GREATER 0)
    message("Core-purity violations found:")
    foreach(v ${violations})
        message("  ${v}")
    endforeach()
    message(FATAL_ERROR "core is not format/vocabulary-neutral (${violation_count} hit(s))")
endif()

message(STATUS "core-purity check: clean (${violation_count} hits across core files)")
