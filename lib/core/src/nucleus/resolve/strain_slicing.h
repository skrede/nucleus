#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_SLICING_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_SLICING_H

#include "nucleus/resolve/strain_relay.h"
#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"
#include "nucleus/resolve/strain_pruning.h"
#include "nucleus/resolve/strain_bucketing.h"
#include "nucleus/resolve/strain_selection.h"
#include "nucleus/resolve/keyed_merge_state.h"
#include "nucleus/resolve/relayed_compaction.h"
#include "nucleus/resolve/strain_layer_rules.h"

#include "nucleus/expected.h"
#include "nucleus/strain_scope.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/config_source/inherit_declaration.h"

#include <string>
#include <vector>
#include <cstddef>
#include <optional>
#include <string_view>

namespace nucleus {

// Collapses keyed-container instances into the ONE unified hierarchy the resolved
// config promises. A primary-key value is internal to resolution -- a transient
// path segment keeping instances distinct through the fold -- and must NEVER
// survive into the frozen keyspace: a key value as a resolved segment would make
// the tree untraversable without knowing the key.
//
// It OWNS the five strain units, which nothing outside the slice consults, and
// BORROWS what they read and write: the building keyspace, the provenance, the
// schema, the repeated sweep, the keyed-merge state, the re-open dispositions and
// the record of containers whose identity the slice satisfies structurally.
class strain_slicing
{
public:
    strain_slicing(keyspace &building, provenance &prov,
                   const schema_registry &schema, repeated_sweep &sweep,
                   const keyed_merge_state &keyed,
                   const std::vector<extend_disposition> &dispositions,
                   std::vector<std::string> &keyed_satisfied) noexcept
        : m_schema(schema)
        , m_keyed_satisfied(keyed_satisfied)
        , m_compaction(building, prov, schema, keyed)
        , m_relay(building, prov, sweep, m_compaction, schema, keyed)
        , m_pruning(building, prov, schema, keyed)
        , m_selection(building, prov, schema)
        , m_layer_rules(building, prov, schema, dispositions)
    {
    }

    // `m_relay` is constructed from `m_compaction`, so a copy or a move would leave the relay driving the source's compaction.
    strain_slicing(const strain_slicing &) = delete;
    strain_slicing &operator=(const strain_slicing &) = delete;
    strain_slicing(strain_slicing &&) = delete;
    strain_slicing &operator=(strain_slicing &&) = delete;
    ~strain_slicing() = default;

    // Runs between fold and validate.
    //
    // With a selection: the matching named strain survives, non-matching named
    // strains are pruned from the keyspace, and the selected strain's entries
    // are re-laid onto the declared (stripped) paths. Selecting a value that
    // matches no strain is a loud error listing every available strain value --
    // including when the container holds no keyed instances at all. Selecting
    // when the schema declares no primary key is a loud error.
    //
    // Without a selection: exactly one named strain auto-resolves (its entries
    // re-laid); several named strains with no selection is a loud error naming
    // the container and every strain; anonymous-only content collapses unchanged.
    //
    // The scope policy applies whenever a strain resolves -- explicitly selected
    // or auto-resolved -- so the two paths cannot diverge for the same strain.
    expected<void, resolve_fold_error>
    slice(const std::optional<std::string> &selection, strain_scope_policy policy)
    {
        if(auto declared = m_selection.require_declared_key(selection); !declared)
            return unexpected(declared.error());

        for(const schema_element &el : m_schema.elements())
        {
            if(!el.identity)
                continue;
            if(auto sliced = slice_container(el, selection, policy); !sliced)
                return sliced;
        }

        return {};
    }

private:
    const schema_registry    &m_schema;
    std::vector<std::string> &m_keyed_satisfied;
    relayed_compaction        m_compaction;
    strain_relay              m_relay;
    strain_pruning            m_pruning;
    strain_selection          m_selection;
    strain_layer_rules        m_layer_rules;

    expected<void, resolve_fold_error>
    slice_container(const schema_element &el,
                    const std::optional<std::string> &selection,
                    strain_scope_policy policy)
    {
        const key_path container = el.container();
        const auto     selected  = m_selection.select(container, selection);
        if(!selected)
            return unexpected(selected.error());
        if(selected.value().strains.empty())
            return {};
        return apply_strain(container, el.declared_path().str(),
                            selected.value(), policy);
    }

    // Bounds the chosen strain, removes what it displaces, and relays it onto
    // the unified hierarchy.
    expected<void, resolve_fold_error>
    apply_strain(const key_path &container, std::string_view identity_path,
                 const strain_selection::chosen_strain &selected,
                 strain_scope_policy policy)
    {
        const strain_buckets &strains = selected.strains;
        const std::string    &chosen  = selected.chosen;
        const std::size_t     Ld      = selected.defining_layer;
        const auto bounds = m_layer_rules.enforce(container, strains, chosen, Ld);
        if(!bounds)
            return unexpected(bounds.error());
        const bool wide = bounds.value().wide_extend;
        m_pruning.prune(container, identity_path, strains, chosen, policy, Ld, wide);
        if(auto r = m_relay.relay_strain(strains.at(chosen), identity_path, policy,
                                         Ld, bounds.value().competitor_layer, wide);
           !r)
            return r;
        // The strain's key value named the instance and was consumed; the
        // enforcer's identity-presence check is satisfied structurally.
        m_keyed_satisfied.push_back(container.str());
        return {};
    }
};

}

#endif
