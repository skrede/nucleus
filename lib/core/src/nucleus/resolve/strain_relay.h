#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_RELAY_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_RELAY_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"
#include "nucleus/resolve/keyed_merge_state.h"
#include "nucleus/resolve/relayed_addressing.h"
#include "nucleus/resolve/relayed_compaction.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/strain_scope.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <string_view>

namespace nucleus {

// Moves one strain's keyed entries onto the unified hierarchy: each is pruned by
// the scope policy, dropped because a higher-rank instance already holds its
// landing site, or written at its unified path with its origin. Whichever way it
// goes the keyed path is withdrawn, so no strain survives the slice in keyed form.
class strain_relay
{
    // Bounds one relay: the identity leaf that is read-only, the scope policy and
    // its two rank bounds, and the extend-wide bypass that spares every chosen entry.
    struct relay_bounds
    {
        std::string_view    identity_path;
        strain_scope_policy policy;
        std::size_t         Ld;
        std::size_t         Ls;
        bool                wide_extend;
    };

    // The two exemptions spare an entry from rank pruning and from displacement.
    struct relayed_entry
    {
        const origin *from;
        std::size_t   entry_rank;
        bool          keyed_merge;
        bool          identity_leaf;
    };

public:
    strain_relay(keyspace &building, provenance &prov, repeated_sweep &sweep,
                 relayed_compaction &compaction, const schema_registry &schema,
                 const keyed_merge_state &keyed) noexcept
        : m_building(building)
        , m_provenance(prov)
        , m_addressing(building, sweep, prov, schema)
        , m_compaction(compaction)
        , m_schema(schema)
        , m_keyed(keyed)
    {
    }

    expected<void, resolve_fold_error>
    relay_strain(const std::vector<key_path> &keyed_paths,
                 std::string_view             identity_path,
                 strain_scope_policy policy, std::size_t Ld, std::size_t Ls,
                 bool wide_extend)
    {
        const relay_bounds          bounds{identity_path, policy, Ld, Ls, wide_extend};
        const std::set<std::string> relayed_containers =
                m_compaction.relayed_container_scopes(keyed_paths,
                                                      m_addressing.begin_relay());
        for(const key_path &keyed : keyed_paths)
        {
            if(auto relayed = relay_one(keyed, bounds); !relayed)
                return relayed;
        }
        return m_compaction.compact_relayed_instances(relayed_containers);
    }

private:
    keyspace                &m_building;
    provenance              &m_provenance;
    relayed_addressing       m_addressing;
    relayed_compaction      &m_compaction;
    const schema_registry   &m_schema;
    const keyed_merge_state &m_keyed;

    expected<void, resolve_fold_error>
    relay_one(const key_path &keyed, const relay_bounds &bounds)
    {
        const relayed_entry entry = classify(keyed, bounds);
        if(excluded(entry, bounds))
            return withdraw(keyed);
        auto unified = m_addressing.unified_path(keyed);
        if(!unified)
            return unexpected(std::move(unified).error());
        // In the strain's bucket but carrying no value: there is nothing to relay.
        const value *v = m_building.find(keyed);
        if(v == nullptr)
            return withdraw(keyed);
        auto displaced = m_addressing.scope_displaced(unified.value(),
                                                      entry.keyed_merge,
                                                      entry.entry_rank);
        if(!displaced)
            return unexpected(std::move(displaced).error());
        if(displaced.value())
            return withdraw(keyed);
        return write_back(keyed, unified.value(), *v, entry);
    }

    relayed_entry classify(const key_path &keyed, const relay_bounds &bounds) const
    {
        const origin     *from       = m_provenance.of(keyed.str());
        const std::size_t entry_rank = from != nullptr ? from->rank : 0;
        const std::string canonical  = m_schema.canonical_text(keyed);
        return relayed_entry{from, entry_rank, m_keyed.under_keyed_merge(canonical),
                             canonical == bounds.identity_path};
    }

    // A keyed-merge collection was finalized across layers before the slice ran, so
    // its instances legitimately span ranks and this pruning must leave them alone:
    // the merge mode overrides the default strain scope freezing.
    static bool excluded(const relayed_entry &entry, const relay_bounds &bounds)
    {
        if(entry.identity_leaf || entry.keyed_merge || bounds.wide_extend)
            return false;
        return bounds.policy == strain_scope_policy::container_open_until_next_strain
                       ? entry.entry_rank >= bounds.Ls
                       : entry.entry_rank > bounds.Ld;
    }

    // A primary key is authoritative and read-only, so a higher-rank flat entry
    // occupying its unified path is a loud error, never a silent skip.
    expected<void, resolve_fold_error>
    write_back(const key_path &keyed, const relayed_addressing::unified_target &unified,
               const value &v, const relayed_entry &entry)
    {
        const origin *at = m_provenance.of(unified.text);
        const bool    displaced =
                !entry.keyed_merge && at != nullptr && at->rank > entry.entry_rank;
        if(displaced && entry.identity_leaf)
            return unexpected(error{errc::layering_violation,
                                    nucleus::format(
                                            "identity field '{}' is read-only; "
                                            "source at rank {} cannot override it",
                                            unified.text, at->rank)});
        if(!displaced)
        {
            m_building.set(unified.path, v);
            if(entry.from != nullptr)
                m_provenance.record(unified.text, *entry.from);
        }
        return withdraw(keyed);
    }

    // The removal and its provenance forget live together so the pair cannot drift.
    expected<void, resolve_fold_error> withdraw(const key_path &keyed)
    {
        m_building.remove(keyed);
        m_provenance.forget(keyed.str());
        return {};
    }
};

}

#endif
