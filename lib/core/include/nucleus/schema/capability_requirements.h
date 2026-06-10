#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CAPABILITY_REQUIREMENTS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CAPABILITY_REQUIREMENTS_H

#include "nucleus/capability.h"

#include "nucleus/schema/schema.h"

#include "nucleus/configuration_source/feature_gate.h"

#include <span>
#include <vector>

namespace nucleus {

// Auto-derives the capability requirements a sealed schema implies from element
// SHAPE alone -- there is no manual declaration verb. A non-root element needs
// nesting (HARD); any repeated element needs duplicate_keys (HARD); any typed
// element would use typed_scalars (SOFT). comments and ordering are never
// requested. The set is deduplicated (N nested elements yield one nesting
// requirement) and emitted in a stable order so callers and tests are
// deterministic. Takes the declared elements directly (the host reaches them via
// configuration_space::schema_elements()), not the registry that owns them.
[[nodiscard]] inline std::vector<feature_requirement>
derive_capability_requirements(std::span<const schema_element> elements)
{
    bool needs_nesting = false;
    bool needs_duplicate_keys = false;
    bool uses_typed_scalars = false;
    for(const schema_element &el : elements)
    {
        if(!el.at.is_root())
            needs_nesting = true;
        if(el.repeated)
            needs_duplicate_keys = true;
        if(el.type_identity.has_value() || el.converter)
            uses_typed_scalars = true;
    }

    std::vector<feature_requirement> out;
    if(needs_nesting)
        out.push_back(feature_requirement{capability::nesting, requirement_strength::required});
    if(needs_duplicate_keys)
        out.push_back(feature_requirement{capability::duplicate_keys, requirement_strength::required});
    if(uses_typed_scalars)
        out.push_back(feature_requirement{capability::typed_scalars, requirement_strength::optional});
    return out;
}

}

#endif
