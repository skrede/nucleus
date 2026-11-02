#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_PRUNING_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_PRUNING_H

#include "nucleus/resolve/strain_bucketing.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/strain_scope.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus {

class strain_pruning
{
    struct prepass_bounds
    {
        std::string                  chosen_prefix;
        std::string_view             identity_path;
        std::size_t                  defining_layer;
        const std::set<std::size_t> &wide_ranks;
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
               const std::set<std::size_t> &wide_ranks)
    {
        prune_unchosen(strains, chosen);
        if(policy != strain_scope_policy::file_level)
            return;
        const std::string chosen_prefix =
            container.str() + key_path::separator + chosen + key_path::separator;
        file_level(prepass_bounds{chosen_prefix, identity_path,
                                  defining_layer, wide_ranks});
    }

private:
    keyspace                &m_building;
    provenance              &m_provenance;
    const schema_registry   &m_schema;
    const keyed_merge_state &m_keyed;

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

    // Sweeps keyed and general entries alike.
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
        // A flat write carries no inheritance layer and always wins by plain stack precedence.
        if(orig != nullptr && !orig->inheritance_layer.has_value())
            return true;
        const std::size_t path_rank = orig != nullptr ? orig->rank : 0;
        if(path_rank == 0 || path_rank <= bounds.defining_layer)
            return true;
        if(m_keyed.under_keyed_merge(m_schema.canonical_text(path)))
            return true;
        if(!path.str().starts_with(bounds.chosen_prefix))
            return false;
        return bounds.wide_ranks.contains(path_rank) || m_schema.canonical_text(path) == bounds.identity_path;
    }

    void drop(const key_path &path)
    {
        m_building.remove(path);
        m_provenance.forget(path.str());
    }
};

}

#endif
