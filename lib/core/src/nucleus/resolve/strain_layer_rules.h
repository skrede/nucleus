#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_LAYER_RULES_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_LAYER_RULES_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/strain_bucketing.h"
#include "nucleus/resolve/strain_unique_fields.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include "nucleus/config_source/inherit_declaration.h"

#include <map>
#include <set>
#include <limits>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// Bounds a chosen strain against the layers around it: the competing strain
// capping its reach, and the cross-layer rules deciding whether it was allowed
// to be introduced at more than one layer at all. It BORROWS the provenance and
// the extend dispositions the fold collected, and owns the unique check.
class strain_layer_rules
{
    using disposition_index =
            std::map<std::pair<std::string, std::string>, extend_strength>;

public:
    // Where the chosen strain's reach ends, and whether its extend disposition
    // is wide enough to spare every one of its entries from the scope prune.
    struct strain_bounds
    {
        std::size_t competitor_layer;
        bool        wide_extend;
    };

    strain_layer_rules(const keyspace &building, const provenance &prov,
                       const schema_registry &schema,
                       const std::vector<extend_disposition> &dispositions) noexcept
        : m_unique(building, schema)
        , m_provenance(prov)
        , m_dispositions(dispositions)
    {
    }

    expected<strain_bounds, resolve_fold_error>
    enforce(const key_path &container, const strain_buckets &strains,
            const std::string &chosen, std::size_t defining_layer) const
    {
        const std::size_t       bound = competitor_layer(strains, chosen, defining_layer);
        const disposition_index disposed = index_dispositions();
        if(auto reopened = check_reopen(container, strains, disposed); !reopened)
            return unexpected(reopened.error());
        if(auto unique = m_unique.check(container, strains); !unique)
            return unexpected(unique.error());
        return strain_bounds{bound, wide(disposed, container, chosen)};
    }

private:
    strain_unique_fields                   m_unique;
    const provenance                      &m_provenance;
    const std::vector<extend_disposition> &m_dispositions;

    // The first layer ABOVE the defining layer that introduces a competing named
    // strain. A competitor introduced at or below it is not the "next" strain and
    // never bounds the chosen one; unbounded when there is none above.
    std::size_t competitor_layer(const strain_buckets &strains,
                                 const std::string &chosen,
                                 std::size_t defining_layer) const
    {
        std::size_t Ls = std::numeric_limits<std::size_t>::max();
        for(const auto &[key_value, paths] : strains)
        {
            if(key_value == chosen)
                continue;
            for(const key_path &keyed : paths)
            {
                const std::size_t *first = m_provenance.first_rank_of(keyed.str());
                if(first != nullptr && *first > defining_layer && *first < Ls)
                    Ls = *first;
            }
        }
        return Ls;
    }

    disposition_index index_dispositions() const
    {
        disposition_index index;
        for(const extend_disposition &d : m_dispositions)
            index[{d.container_path, d.key_value}] = d.strength;
        return index;
    }

    static bool wide(const disposition_index &disposed, const key_path &container,
                     const std::string &chosen)
    {
        auto it = disposed.find({container.str(), chosen});
        return it != disposed.end() && it->second == extend_strength::wide;
    }

    // These rules apply only to inheritance-chain entries, identified by the
    // explicit inheritance-layer channel on each origin. Flat source layering
    // (env, argv, defaults) carries no inheritance layer, forms a single flat
    // layer by design, and is never a re-open error.
    expected<void, resolve_fold_error>
    check_reopen(const key_path &container, const strain_buckets &strains,
                 const disposition_index &disposed) const
    {
        for(const auto &[key_value, paths] : strains)
        {
            const std::set<std::size_t> layers = inheritance_layers(paths);
            if(layers.empty())
                continue;
            auto checked = check_strain(container, key_value, layers.size() > 1, disposed);
            if(!checked)
                return unexpected(checked.error());
        }
        return {};
    }

    // Both the first-introduction layer and the winning layer count: an entry
    // overwritten by a higher chain layer records the base and the deriving file
    // alike, so a re-open via overwrite still reads as multi-layer even when the
    // overwrite collapses the building keyspace to a single path.
    std::set<std::size_t> inheritance_layers(const std::vector<key_path> &paths) const
    {
        std::set<std::size_t> layers;
        for(const key_path &kp : paths)
        {
            const std::size_t *first = m_provenance.first_inheritance_layer_of(kp.str());
            if(first != nullptr)
                layers.insert(*first);
            const origin *win = m_provenance.of(kp.str());
            if(win != nullptr && win->inheritance_layer.has_value())
                layers.insert(win->inheritance_layer.value());
        }
        return layers;
    }

    static expected<void, resolve_fold_error>
    check_strain(const key_path &container, const std::string &key_value,
                 bool cross_layer, const disposition_index &disposed)
    {
        const bool has_disposition = disposed.contains({container.str(), key_value});
        if(cross_layer && !has_disposition)
            return unexpected(error{errc::layering_violation, nucleus::format(
                "primary-key value '{}' in container '{}' is introduced "
                "at multiple layers without an extend disposition: "
                "re-opening a named instance in a derived file requires "
                "an explicit extend attribute",
                key_value, container.str())});
        if(has_disposition && !cross_layer)
            return unexpected(error{errc::layering_violation, nucleus::format(
                "extend disposition for '{}' in container '{}' has no "
                "base: no layer below the extending layer provides "
                "entries for this instance",
                key_value, container.str())});
        return {};
    }
};

}

#endif
