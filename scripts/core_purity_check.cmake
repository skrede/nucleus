# Core-purity check.
#
# The core must be domain-neutral and format-agnostic: no XML / pugixml coupling
# and no host vocabulary leaking into it. Parser dependencies live only inside
# their own per-target modules (e.g. lib/xml/), never in core. This script greps
# the relocated core headers and implementation for forbidden symbols and fails
# (nonzero exit) on any hit. Run as a CTest gate.
#
# Invoke: cmake -DNUCLEUS_ROOT=<repo> -P scripts/core_purity_check.cmake

if(NOT DEFINED NUCLEUS_ROOT)
    set(NUCLEUS_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

# Forbidden vocabulary. Case-insensitive substrings that must not appear in core:
# any format NAME (xml), the parser DEPENDENCY (pugi/pugixml), and host-specific
# (vagus/node-vocabulary) symbols. The core is format-agnostic -- it must not name a
# format at all; the output seam is a format-agnostic concept, and every per-format
# emitter lives in its own quarantined module. Kept in lockstep with the shell gate
# (scripts/core_purity_check.sh).
set(forbidden "xml" "pugi" "vagus" "<vagus>" "<node>")

# Directories that are allowed to mention parser/host vocabulary because they ARE
# the quarantined module: the xml format module (lib/xml/) wraps pugixml privately.
# It lives outside lib/core entirely, so it is not even scanned here; this exclusion
# is belt-and-suspenders. Paths are normalized to absolute real paths so the
# exclusion holds however NUCLEUS_ROOT was passed (absolute, ".", or with embedded
# "/./" segments).
get_filename_component(quarantined_xml_dir
    "${NUCLEUS_ROOT}/lib/xml" REALPATH)
set(quarantined_dirs "${quarantined_xml_dir}")

file(GLOB_RECURSE core_files
    "${NUCLEUS_ROOT}/lib/core/include/nucleus/*.h"
    "${NUCLEUS_ROOT}/lib/core/src/nucleus/*.h"
    "${NUCLEUS_ROOT}/lib/core/src/nucleus/*.cpp"
)

set(violations "")

foreach(file ${core_files})
    get_filename_component(file "${file}" REALPATH)
    set(is_quarantined FALSE)
    foreach(dir ${quarantined_dirs})
        # Plain substring test (not regex) so "." and other metacharacters in the
        # path cannot weaken or break the quarantine match.
        string(FIND "${file}" "${dir}/" quarantine_position)
        if(quarantine_position EQUAL 0)
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
