#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_SELECTION_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_SELECTION_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/strain_bucketing.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include <string>
#include <cstddef>
#include <utility>
#include <optional>

namespace nucleus {

class strain_selection
{
public:
    // Empty strains means the container holds no keyed instances and nothing downstream applies to it.
    struct chosen_strain
    {
        strain_buckets strains;
        std::string    chosen;
        std::size_t    defining_layer;
    };

    strain_selection(const keyspace &building, const provenance &prov,
                     const schema_registry &schema) noexcept
        : m_bucketing(building, schema)
        , m_provenance(prov)
        , m_schema(schema)
    {
    }

    // Checked before the per-element loop, which runs only for identity elements and so would never
    // fire when there are none.
    expected<void, resolve_fold_error>
    require_declared_key(const std::optional<std::string> &selection) const
    {
        if(!selection.has_value())
            return {};
        for(const schema_element &el : m_schema.elements())
            if(el.identity)
                return {};
        return unexpected(error{errc::invalid_selection, nucleus::format(
            "selection '{}' cannot be applied: the schema declares "
            "no primary key",
            selection.value())});
    }

    expected<chosen_strain, resolve_fold_error>
    select(const key_path &container, const std::optional<std::string> &selection) const
    {
        auto bucketed = m_bucketing.bucket(container);
        if(!bucketed)
            return unexpected(bucketed.error());
        if(bucketed.value().empty())
            return unkeyed(container, selection);
        return resolve_chosen(container, std::move(bucketed).value(), selection);
    }

private:
    strain_bucketing       m_bucketing;
    const provenance      &m_provenance;
    const schema_registry &m_schema;

    static expected<chosen_strain, resolve_fold_error>
    unkeyed(const key_path &container, const std::optional<std::string> &selection)
    {
        if(!selection.has_value())
            return chosen_strain{};
        return unexpected(error{errc::invalid_selection, nucleus::format(
            "selection '{}' does not match any strain in container "
            "'{}': the container holds no primary-keyed instances",
            selection.value(), container.str())});
    }

    expected<chosen_strain, resolve_fold_error>
    resolve_chosen(const key_path &container, strain_buckets &&strains,
                   const std::optional<std::string> &selection) const
    {
        auto chosen = choose(container, strains, selection);
        if(!chosen)
            return unexpected(chosen.error());
        auto defining = defining_layer(container, strains, chosen.value());
        if(!defining)
            return unexpected(defining.error());
        return chosen_strain{std::move(strains), std::move(chosen).value(),
                             defining.value()};
    }

    static expected<std::string, resolve_fold_error>
    choose(const key_path &container, const strain_buckets &strains,
           const std::optional<std::string> &selection)
    {
        if(selection.has_value())
        {
            if(strains.contains(selection.value()))
                return selection.value();
            return unexpected(error{errc::invalid_selection, nucleus::format(
                "selection '{}' does not match any strain in container "
                "'{}'; available: {}",
                selection.value(), container.str(), strain_values(strains))});
        }
        if(strains.size() > 1)
            return unexpected(error{errc::invalid_selection, nucleus::format(
                "container '{}' holds {} primary-keyed instances ({}) and "
                "no instance is selected: a config space resolves "
                "exactly one",
                container.str(), strains.size(), strain_values(strains))});
        return strains.begin()->first;
    }

    // First-introduction ranks, so a later overwrite does not move the defining layer. The fold
    // records provenance with every set, so an entry with no recorded rank is an invariant violation.
    expected<std::size_t, resolve_fold_error>
    defining_layer(const key_path &container, const strain_buckets &strains,
                   const std::string &chosen) const
    {
        std::size_t lowest    = 0;
        bool        found_any = false;
        for(const key_path &keyed : strains.at(chosen))
        {
            const std::size_t *first = m_provenance.first_rank_of(keyed.str());
            if(first != nullptr && (!found_any || *first < lowest))
            {
                lowest    = *first;
                found_any = true;
            }
        }
        if(found_any)
            return lowest;
        return unexpected(error{errc::layering_violation, nucleus::format(
            "strain '{}' in container '{}' has no recorded "
            "provenance: resolve cannot bound its defining layer",
            chosen, container.str())});
    }
};

}

#endif
