#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RESOLVE_TYPES_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RESOLVE_TYPES_H

#include "nucleus/error.h"
#include "nucleus/identity.h"

#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/inherit_declaration.h"

#include <string>
#include <cstddef>
#include <optional>
#include <filesystem>

namespace nucleus {

using resolve_fold_error = error;

// Bounds a chain of references across leaves; the nesting within one value is capped separately.
inline constexpr std::size_t default_reference_depth_cap = 64;

// The declaring layer's rank, so an extend strength governs that document's own content rather
// than every layer of the strain.
struct ranked_disposition
{
    extend_disposition disposition;
    std::size_t        rank;
};

struct layered_handle
{
    source_handle *handle;
    std::size_t    rank;
    std::string    label;
    owner_token    owner;
    // Present only for inheritance-chain entries, base lowest; a flat source leaves it absent and
    // forms one flat layer exempt from the slice's re-open rules.
    std::optional<std::size_t>          inheritance_layer;
    // Set only for document sources.
    std::optional<std::filesystem::path> origin_file;
    // The batch the inheritance walk already pulled for a chain layer, so the fold does not pull
    // its handle twice; null for a layer with no walk.
    config_source_batch *cached_batch = nullptr;
};

}

#endif
