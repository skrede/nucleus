#ifndef HPP_GUARD_NUCLEUS_RESOLVE_KEYED_DIVERT_H
#define HPP_GUARD_NUCLEUS_RESOLVE_KEYED_DIVERT_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <optional>

namespace nucleus {

// Recognizes the leaves belonging to a keyed-merge collection and parks them on
// the merge accumulator rather than letting the fold's sweep path store them:
// their final ordinals are not known until every layer has arrived. It also owns
// the per-layer grouping that turns flat (non-indexed) arrivals into instances.
// State and schema are borrowed, and outlive this resolve-lifetime collaborator.
class keyed_divert
{
    // The container's declared form, its actual path, and the leaf below it.
    struct divert_site
    {
        std::string canonical;
        std::string container;
        std::string suffix;
    };

public:
    keyed_divert(keyed_merge_state &keyed, const schema_registry &schema) noexcept
        : m_keyed(keyed)
        , m_schema(schema)
    {
    }

    // A repeating suffix marks an instance boundary only within one layer.
    void reset() noexcept
    {
        m_flat_counter.clear();
        m_flat_suffixes_seen.clear();
    }

    // True when the path lies under a container declaring a keyed merge mode; a
    // false return leaves `value` untouched for the fold's sweep path.
    expected<bool, resolve_fold_error>
    divert(const key_path &path, const keyspace_entry &entry, std::string &value,
           const layered_handle &layer)
    {
        if(!m_keyed.any_declared())
            return false;
        const std::vector<std::string> &segs = path.segments();
        for(std::size_t len = 1; len <= segs.size(); ++len)
        {
            const std::optional<divert_site> site = site_at(segs, len);
            if(!site)
                continue;
            auto ordinal = ordinal_at(*site, segs[len - 1], entry, layer.label);
            if(!ordinal)
                return unexpected(ordinal.error());
            m_keyed.divert(site->container, site->canonical,
                keyed_merge_state::keyed_instance_entry{
                    layer.rank, static_cast<std::size_t>(ordinal.value()), site->suffix, std::move(value),
                    origin{layer.rank, layer.label, layer.owner, layer.inheritance_layer}});
            return true;
        }
        return false;
    }

private:
    keyed_merge_state       &m_keyed;
    const schema_registry   &m_schema;
    // Keyed by ACTUAL container path (key segments still present pre-slice).
    std::map<std::string, std::size_t> m_flat_counter;
    std::map<std::string, std::set<std::string>> m_flat_suffixes_seen;

    std::optional<divert_site>
    site_at(const std::vector<std::string> &segs, std::size_t len) const
    {
        const auto prefix = key_path::parse(join_range(segs, 0, len));
        if(!prefix)
            return std::nullopt;
        const std::string canonical = m_schema.canonical_text(prefix.value());
        if(!m_keyed.declares(canonical))
            return std::nullopt;
        const std::string container =
            join_segment(join_range(segs, 0, len - 1),
                         std::string(key_path::base_name(segs[len - 1])));
        return divert_site{canonical, container, join_range(segs, len, segs.size())};
    }

    expected<std::uint64_t, resolve_fold_error>
    ordinal_at(const divert_site &site, const std::string &segment,
               const keyspace_entry &entry, const std::string &label)
    {
        if(key_path::is_indexed_segment(segment))
            return key_path::ordinal_of(segment);
        return flat_ordinal(site, entry, label);
    }

    // A suffix repeating within the current instance means a NEW instance starts.
    expected<std::uint64_t, resolve_fold_error>
    flat_ordinal(const divert_site &site, const keyspace_entry &entry,
                 const std::string &label)
    {
        std::set<std::string> &seen = m_flat_suffixes_seen[site.container];
        if(!seen.contains(site.suffix))
        {
            seen.insert(site.suffix);
            return m_flat_counter[site.container];
        }
        if(!entry.capabilities.supports(capability::duplicate_keys))
            return unexpected(error{errc::layering_violation, nucleus::format(
                "source '{}': flat entry '{}' cannot address keyed "
                "collection '{}': a source without duplicate_keys can "
                "supply at most one instance's worth of leaves per layer",
                label, entry.path, site.canonical)});
        if(auto ordered = require_instance_major(site, seen, label); !ordered)
            return unexpected(ordered.error());
        seen.clear();
        seen.insert(site.suffix);
        return ++m_flat_counter[site.container];
    }

    // The flat grouping REQUIRES instance-major arrival (all of one instance's
    // fields before the next instance's begin); a suffix reappearing before every
    // declared field has been seen means the source emitted fields field-major
    // instead, which cannot be safely disambiguated -- fail rather than mis-pair.
    expected<void, resolve_fold_error>
    require_instance_major(const divert_site &site, const std::set<std::string> &seen,
                           const std::string &label) const
    {
        const std::set<std::string> &declared = m_keyed.declared_suffixes(site.canonical);
        if(declared.empty() || seen == declared)
            return {};
        return unexpected(error{errc::layering_violation, nucleus::format(
            "source '{}': flat entries for keyed collection '{}' "
            "must supply every field of one instance before the "
            "next instance begins (instance-major order); field "
            "'{}' repeated after only {} of {} declared fields were "
            "seen for the current instance",
            label, site.canonical, site.suffix, seen.size(), declared.size())});
    }

    static std::string join_range(const std::vector<std::string> &segs,
                                  std::size_t first, std::size_t last)
    {
        std::string joined;
        for(std::size_t i = first; i < last; ++i)
        {
            if(!joined.empty())
                joined += key_path::separator;
            joined += segs[i];
        }
        return joined;
    }
};

}

#endif
