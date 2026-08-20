#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RESOLVE_TYPES_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RESOLVE_TYPES_H

#include "nucleus/error.h"
#include "nucleus/identity.h"

#include "nucleus/config_source/source_handle.h"

#include <string>
#include <cstddef>
#include <optional>
#include <filesystem>

namespace nucleus {

// The error a resolve fold can report: a source pull failure or a token
// resolution failure, surfaced verbatim with the offending layer named.
using resolve_fold_error = error;

// Maximum total reference substitutions across one pass-2 resolve. Stops billion-laughs amplification.
inline constexpr std::size_t default_reference_budget = 10000;
// Maximum cross-leaf reference chain depth. Per-value nesting cap (16) stays in expansion_guard.h.
inline constexpr std::size_t default_reference_depth_cap = 64;

// One entry in the handle-based fold: the erased source, its ascending rank
// (cross-source precedence), a human-readable label for provenance, and an
// optional inheritance-chain layer ordinal. The layer is present only for
// inheritance-chain entries (base lowest); flat sources leave it absent and
// are treated as a single flat layer exempt from the slice re-open rules.
struct layered_handle
{
    source_handle *handle;
    std::size_t    rank;
    std::string    label;
    owner_token    owner;
    std::optional<std::size_t>          inheritance_layer;
    // Set for document sources (derives from entries[i].path); absent for stack/argv/env sources.
    std::optional<std::filesystem::path> origin_file;
    // Set for chain-layer handles: points at the batch chain_walker already
    // pulled during the inheritance walk, so fold() consumes it instead of
    // pulling the same handle a second time. Null for stack/argv/env layers,
    // which have no walk phase and are pulled here for the first time.
    config_source_batch *cached_batch = nullptr;
};

}

#endif
