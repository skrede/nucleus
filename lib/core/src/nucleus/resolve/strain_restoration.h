#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_RESTORATION_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_RESTORATION_H

#include "nucleus/resolve/displaced_values.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/strain_scope.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string_view>

namespace nucleus {

struct strain_scope_bounds
{
    std::string_view             identity_path;
    strain_scope_policy          policy;
    std::size_t                  Ld;
    std::size_t                  Ls;
    const std::set<std::size_t> &wide_ranks;
};

// Tested per supplying rank rather than once per strain, because a wide re-open admits only the
// content of the document that declared it.
inline bool scope_excludes(strain_scope_policy policy, std::size_t rank, std::size_t Ld,
                           std::size_t Ls, const std::set<std::size_t> &wide_ranks)
{
    if(wide_ranks.contains(rank))
        return false;
    if(policy == strain_scope_policy::container_open_until_next_strain)
        return rank >= Ls;
    return rank > Ld;
}

// Discards what the scope policy excludes from the chosen strain and puts back what the excluded
// entries displaced during the fold, so the strain resolves as its admissible layers left it.
// Under file_level the same holds for general entries a chain layer above Ld displaced.
class strain_restoration
{
    enum class region
    {
        chosen,
        general,
        outside,
    };

public:
    strain_restoration(keyspace &building, provenance &prov, const schema_registry &schema,
                       const keyed_merge_state &keyed, const displaced_values &displaced) noexcept
            : m_building(building)
            , m_provenance(prov)
            , m_schema(schema)
            , m_keyed(keyed)
            , m_displaced(displaced)
    {
    }

    void restore(const std::string &chosen_prefix, const std::vector<key_path> &chosen_paths,
                 const strain_scope_bounds &bounds)
    {
        withdraw_excluded(chosen_paths, bounds);
        restore_displaced(chosen_prefix, bounds);
    }

private:
    keyspace                &m_building;
    provenance              &m_provenance;
    const schema_registry   &m_schema;
    const keyed_merge_state &m_keyed;
    const displaced_values  &m_displaced;

    void withdraw_excluded(const std::vector<key_path> &chosen_paths, const strain_scope_bounds &bounds)
    {
        for(const key_path &path : chosen_paths)
        {
            const origin     *from      = m_provenance.of(path.str());
            const std::string canonical = m_schema.canonical_text(path);
            if(from == nullptr || canonical == bounds.identity_path || m_keyed.under_keyed_merge(canonical))
                continue;
            if(scope_excludes(bounds.policy, from->rank, bounds.Ld, bounds.Ls, bounds.wide_ranks))
            {
                m_building.remove(path);
                m_provenance.forget(path.str());
            }
        }
    }

    // Newest first, so a path takes back its highest admissible version.
    void restore_displaced(const std::string &chosen_prefix, const strain_scope_bounds &bounds)
    {
        const std::vector<displaced_value> &displaced = m_displaced.all();
        std::vector<region>                 regions;
        std::map<std::string, std::size_t>  replaced_at = swept_at(chosen_prefix, bounds);
        for(const displaced_value &d : displaced)
        {
            regions.push_back(region_of(d.path, chosen_prefix, bounds));
            if(admits(regions.back(), d.displaced_by, d.displaced_by_chain, bounds))
                replaced_at[d.unit] = std::max(replaced_at[d.unit], d.displaced_by);
        }
        for(std::size_t i = displaced.size(); i-- > 0;)
            if(restorable(displaced[i], regions[i], replaced_at, bounds))
            {
                m_building.set(displaced[i].path, displaced[i].val);
                m_provenance.record(displaced[i].path.str(), displaced[i].prov);
            }
    }

    // An admissible sweep counts even when an excluded layer had already emptied the instance.
    std::map<std::string, std::size_t> swept_at(const std::string         &chosen_prefix,
                                                const strain_scope_bounds &bounds) const
    {
        std::map<std::string, std::size_t> swept;
        for(const swept_unit &s : m_displaced.sweeps())
        {
            const auto unit = key_path::parse(s.unit);
            if(unit && admits(region_of(unit.value(), chosen_prefix, bounds), s.swept_by, s.swept_by_chain, bounds))
                swept[s.unit] = std::max(swept[s.unit], s.swept_by);
        }
        return swept;
    }

    // A value stays out when an admissible layer later replaced its unit or an instance enclosing
    // it, which an excluded layer between the two had hidden.
    bool restorable(const displaced_value &d, region where,
                    const std::map<std::string, std::size_t> &replaced_at,
                    const strain_scope_bounds                &bounds) const
    {
        if(where == region::outside || m_building.contains(d.path))
            return false;
        if(!admits(where, d.prov.rank, d.prov.inheritance_layer.has_value(), bounds))
            return false;
        if(admits(where, d.displaced_by, d.displaced_by_chain, bounds))
            return false;
        return !superseded(d, replaced_at);
    }

    // A unit ends right before a separator or an ordinal bracket, so a leaf list's unit covers
    // its ordinals.
    static bool superseded(const displaced_value &d, const std::map<std::string, std::size_t> &replaced_at)
    {
        const std::string path = d.path.str();
        for(std::size_t end = 1; end <= path.size(); ++end)
        {
            if(end < path.size() && path[end] != key_path::separator && path[end] != '[')
                continue;
            const auto replaced = replaced_at.find(path.substr(0, end));
            if(replaced != replaced_at.end() && replaced->second > d.prov.rank)
                return true;
        }
        return false;
    }

    region region_of(const key_path &path, const std::string &chosen_prefix,
                     const strain_scope_bounds &bounds) const
    {
        const std::string canonical = m_schema.canonical_text(path);
        if(canonical == bounds.identity_path || m_keyed.under_keyed_merge(canonical))
            return region::outside;
        if(path.str().starts_with(chosen_prefix))
            return region::chosen;
        if(bounds.policy != strain_scope_policy::file_level || under_keyed_container(canonical))
            return region::outside;
        return region::general;
    }

    // A flat source is exempt from the file_level freeze of general entries, as the pruning is.
    static bool admits(region where, std::size_t rank, bool chain, const strain_scope_bounds &bounds)
    {
        if(where == region::chosen)
            return !scope_excludes(bounds.policy, rank, bounds.Ld, bounds.Ls, bounds.wide_ranks);
        return !chain || rank <= bounds.Ld;
    }

    bool under_keyed_container(const std::string &canonical) const
    {
        for(const schema_element &el : m_schema.elements())
            if(el.identity && canonical.starts_with(el.container().str() + key_path::separator))
                return true;
        return false;
    }
};

}

#endif
