#ifndef HPP_GUARD_NUCLEUS_RESOLVE_REPEATED_PLACEMENT_H
#define HPP_GUARD_NUCLEUS_RESOLVE_REPEATED_PLACEMENT_H

#include "nucleus/resolve/layer_fold.h"
#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/key_path.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include <string>
#include <utility>

namespace nucleus {

// Places the entries that land under a declared repeated scope. Which unit an
// entry belongs to, which existing entries it displaces and how it is written are
// the sweep collaborator's rules; what this unit adds is the per-layer discipline
// around them -- one sweep per instance per layer, one keyspace scan per scope,
// and a minted ordinal for a repeated leaf that arrived without one.
class repeated_placement
{
    // The borrowed inputs one entry's placement steps share, plus the repeated
    // scope resolved for it, so every step keeps a one-line signature.
    struct entry_site
    {
        const key_path       &path;
        const keyspace_entry &entry;
        const layered_handle &layer;
        std::string           scope;
        std::string           sweep_key;
        bool                  scope_is_leaf;
    };

public:
    repeated_placement(layer_fold &layers, repeated_sweep &sweep,
                       const schema_registry &schema) noexcept
        : m_layers(layers)
        , m_sweep(sweep)
        , m_schema(schema)
    {
    }

    // True when the path lies under a declared repeated scope, in which case the
    // entry has been swept into place and `text` moved out; a false return leaves
    // `text` untouched for the fold's plain store.
    expected<bool, resolve_fold_error>
    place(const key_path &path, const keyspace_entry &entry, std::string &text,
          const layered_handle &layer)
    {
        const std::string canonical = m_schema.canonical_text(path);
        const std::string scope =
            repeated_scope_of(m_layers.repeated_declared(), canonical);
        if(scope.empty())
            return false;
        // A repeated leaf has no declared sub-structure, so an entry deeper than
        // the leaf itself addresses no instance of it and stores plainly.
        const bool scope_is_leaf = !m_layers.repeated_containers().contains(scope);
        if(scope_is_leaf && canonical != scope)
            return false;
        entry_site site{path, entry, layer, scope, {}, scope_is_leaf};
        if(auto swept = sweep_and_store(site, text); !swept)
            return unexpected(swept.error());
        return true;
    }

private:
    layer_fold              &m_layers;
    repeated_sweep          &m_sweep;
    const schema_registry   &m_schema;

    expected<void, resolve_fold_error>
    sweep_and_store(entry_site &site, std::string &text)
    {
        if(auto keyed = fill_sweep_key(site); !keyed)
            return unexpected(keyed.error());
        if(auto addressed = reject_unaddressed(site); !addressed)
            return unexpected(addressed.error());
        auto target = sweep_target(site);
        if(!target)
            return unexpected(std::move(target).error());
        sweep_once(site);
        m_sweep.store_entry(target.value(), std::move(text), site.layer);
        return {};
    }

    expected<void, resolve_fold_error> fill_sweep_key(entry_site &site) const
    {
        site.sweep_key = m_sweep.sweep_key_of(site.path, site.scope, site.scope_is_leaf);
        if(!site.sweep_key.empty())
            return {};
        return unexpected(error{errc::malformed_source, nucleus::format(
            "internal invariant violation: no prefix of '{}' canonicalizes "
            "to its declared repeated scope '{}' in fold()'s sweep",
            site.entry.path, site.scope)});
    }

    expected<void, resolve_fold_error> reject_unaddressed(const entry_site &site) const
    {
        const std::string unaddressed = m_sweep.unindexed_repeated_container(
            m_layers.repeated_containers(), site.path);
        if(unaddressed.empty())
            return {};
        return unexpected(error{errc::malformed_source, nucleus::format(
            "source '{}': key path '{}' addresses repeated container '{}' "
            "without naming an instance",
            site.layer.label, site.entry.path, unaddressed)});
    }

    expected<key_path, resolve_fold_error> sweep_target(const entry_site &site)
    {
        if(!site.scope_is_leaf || key_path::is_indexed_segment(site.path.leaf()))
            return site.path;
        return repeated_sweep::mint_leaf_ordinal(site.entry, site.sweep_key,
                                                 site.layer.label,
                                                 m_layers.state().leaf_ordinals);
    }

    // The scope's buckets come from one keyspace scan the first time a layer
    // touches the scope, so an instance sweep never rescans the keyspace.
    void sweep_once(const entry_site &site)
    {
        auto &state = m_layers.state();
        if(state.swept.contains(site.sweep_key))
            return;
        state.swept.insert(site.sweep_key);
        if(!state.buckets.contains(site.scope))
            state.buckets[site.scope] =
                m_sweep.bucket_by_instance(site.scope, site.scope_is_leaf);
        m_sweep.sweep_instance(state.buckets[site.scope][site.sweep_key]);
    }
};

}

#endif
