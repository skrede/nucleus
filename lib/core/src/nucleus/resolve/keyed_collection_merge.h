#ifndef HPP_GUARD_NUCLEUS_RESOLVE_KEYED_COLLECTION_MERGE_H
#define HPP_GUARD_NUCLEUS_RESOLVE_KEYED_COLLECTION_MERGE_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/keyed_merge_rules.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>

namespace nucleus {

// Drains the accumulator the divert filled, applying each collection's merge mode
// by the identity-group key VALUE (never by ordinal), then re-indexing the
// survivors onto contiguous ordinals and carrying provenance through every move.
// wholesale_replace never arrives here -- it stays on the fold's sweep path. The
// keyspace, provenance and merge state are borrowed, and outlive this collaborator.
class keyed_collection_merge
{
public:
    keyed_collection_merge(keyspace &building, provenance &prov,
                           keyed_merge_state &keyed) noexcept
        : m_building(building)
        , m_provenance(prov)
        , m_keyed(keyed)
    {
    }

    // Runs between fold() and slice(), so the merge sees every layer while the key
    // is still present; slice() later strips the transient strain-key segments.
    expected<void, resolve_fold_error> merge()
    {
        for(auto &[container, entries] : m_keyed.accumulator())
        {
            const std::string &canonical = m_keyed.canonical_of_actual(container);
            const std::string &field = m_keyed.field_of(canonical);
            std::map<keyed_merge_rules::instance_key, keyed_merge_rules::merged_instance>
                grouped = keyed_merge_rules::group_by_instance(entries, field);
            const std::vector<keyed_merge_rules::merged_instance *> instances =
                keyed_merge_rules::ordered_instances(grouped);
            if(auto keyed = keyed_merge_rules::require_merge_key(instances, canonical, field); !keyed)
                return unexpected(keyed.error());
            auto survivors = m_keyed.mode_of(canonical) == merge_mode::unite
                ? keyed_merge_rules::united_survivors(instances, canonical, field)
                : keyed_merge_rules::replaced_survivors(instances, canonical, field);
            if(!survivors)
                return unexpected(survivors.error());
            if(auto laid = reindex(container, survivors.value()); !laid)
                return unexpected(laid.error());
        }
        return {};
    }

private:
    keyspace            &m_building;
    provenance          &m_provenance;
    keyed_merge_state   &m_keyed;

    expected<void, resolve_fold_error>
    reindex(const std::string &container,
            const std::vector<keyed_merge_rules::merged_instance *> &survivors)
    {
        std::size_t new_ordinal = 0;
        for(const keyed_merge_rules::merged_instance *mi : survivors)
        {
            const std::string base =
                container + "[" + std::to_string(new_ordinal) + "]";
            for(const keyed_merge_state::keyed_instance_entry *leaf : mi->leaves)
            {
                const std::string new_path = leaf->suffix.empty()
                    ? base : base + key_path::separator + leaf->suffix;
                auto kp = key_path::parse(new_path);
                if(!kp)
                    return unexpected(error{errc::malformed_source, nucleus::format(
                        "internal invariant violation: re-parsed path failed to parse "
                        "in merge_keyed_collections()'s rebuild: '{}'", new_path)});
                m_building.set(kp.value(), value::owned(leaf->value));
                m_provenance.record(new_path, leaf->prov);
            }
            ++new_ordinal;
        }
        return {};
    }
};

}

#endif
