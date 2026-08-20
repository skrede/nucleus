#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_PRUNING_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_PRUNING_H

#include "nucleus/resolve/strain_bucketing.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/strain_scope.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus {

// Removes what the chosen strain displaces: every non-chosen strain's keyed
// paths, and -- under the file_level policy -- every remaining path the chosen
// strain's defining layer outranks. It BORROWS the building keyspace and the
// provenance, which it is the only strain unit to MUTATE, plus the schema it
// canonicalizes paths through and the keyed-merge state it asks which
// collections were already finalised across layers.
class strain_pruning
{
    // What the file_level pre-pass measures each path against.
    struct prepass_bounds
    {
        std::string      chosen_prefix;
        std::string_view identity_path;
        std::size_t      defining_layer;
        bool             wide_extend;
    };

public:
    strain_pruning(keyspace &building, provenance &prov,
                   const schema_registry &schema,
                   const keyed_merge_state &keyed) noexcept
        : m_building(building)
        , m_provenance(prov)
        , m_schema(schema)
        , m_keyed(keyed)
    {
    }

    void prune(const key_path &container, std::string_view identity_path,
               const strain_buckets &strains, const std::string &chosen,
               strain_scope_policy policy, std::size_t defining_layer,
               bool wide_extend)
    {
        prune_unchosen(strains, chosen);
        if(policy != strain_scope_policy::file_level)
            return;
        const std::string chosen_prefix =
            container.str() + key_path::separator + chosen + key_path::separator;
        file_level(prepass_bounds{chosen_prefix, identity_path,
                                  defining_layer, wide_extend});
    }

private:
    void prune_unchosen(const strain_buckets &strains, const std::string &chosen)
    {
        for(const auto &[key_value, paths] : strains)
        {
            if(key_value == chosen)
                continue;
            for(const key_path &keyed : paths)
                drop(keyed);
        }
    }

    // Sweeps keyed and general entries alike, and walks a snapshot so that
    // removal cannot invalidate the walk.
    void file_level(const prepass_bounds &bounds)
    {
        const std::vector<key_path> snapshot = m_building.paths();
        for(const key_path &path : snapshot)
            if(!exempt(path, bounds))
                drop(path);
    }

    bool exempt(const key_path &path, const prepass_bounds &bounds) const
    {
        const origin *orig = m_provenance.of(path.str());
        // Flat unified-path writes (argv/env) carry no inheritance_layer and
        // always win by plain stack precedence, per strain_scope.h's documented
        // contract; exempt them from the rank-bounded prune.
        if(orig != nullptr && !orig->inheritance_layer.has_value())
            return true;
        const std::size_t path_rank = orig != nullptr ? orig->rank : 0;
        if(path_rank == 0 || path_rank <= bounds.defining_layer)
            return true;
        // Keyed-merge collections were finalised across layers already; never
        // rank-prune them here either.
        if(m_keyed.under_keyed_merge(m_schema.canonical_text(path)))
            return true;
        // The chosen identity always survives; extend-wide preserves every other
        // chosen entry, which must compose regardless of the scope policy.
        return path.str().starts_with(bounds.chosen_prefix)
               && (bounds.wide_extend
                   || m_schema.canonical_text(path) == bounds.identity_path);
    }

    void drop(const key_path &path)
    {
        m_building.remove(path);
        m_provenance.forget(path.str());
    }

    keyspace                &m_building;
    provenance              &m_provenance;
    const schema_registry   &m_schema;
    const keyed_merge_state &m_keyed;
};

}

#endif
