#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RELAYED_COMPACTION_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RELAYED_COMPACTION_H

#include "nucleus/resolve/relayed_move.h"
#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <algorithm>

namespace nucleus {

// Closes the ordinal gaps a relay leaves behind: an excluded keyed instance is
// removed outright, so a repeated container's surviving instances can carry a hole
// in their ordinal run, and this unit renumbers them back to a dense sequence.
// Repeated leaves are whole value-list units and cannot form an interior gap, so
// only declared containers are considered.
class relayed_compaction
{
    using relayed_instance_groups =
            std::map<std::string, std::vector<std::pair<std::size_t, key_path>>>;

public:
    relayed_compaction(keyspace &building, provenance &prov,
                       const schema_registry &schema,
                       const keyed_merge_state &keyed) noexcept
        : m_move(building, prov)
        , m_building(building)
        , m_schema(schema)
        , m_keyed(keyed)
    {
    }

    // A keyed-merge collection is relayed verbatim, so it never gains a gap.
    std::set<std::string>
    relayed_container_scopes(const std::vector<key_path> &paths,
                             const std::set<std::string> &containers) const
    {
        std::set<std::string> scopes;
        for(const key_path &path : paths)
        {
            const std::string canonical = m_schema.canonical_text(path);
            if(m_keyed.under_keyed_merge(canonical))
                continue;
            for(const std::string &scope : containers)
                if(canonical == scope || canonical.starts_with(scope + key_path::separator))
                    scopes.insert(scope);
        }
        return scopes;
    }

    expected<void, resolve_fold_error>
    compact_relayed_instances(const std::set<std::string> &scopes)
    {
        auto declared = ordered_relayed_scopes(scopes);
        if(!declared)
            return unexpected(std::move(declared).error());
        for(const key_path &scope : declared.value())
        {
            if(auto compacted = compact_relayed_scope(scope); !compacted)
                return compacted;
        }
        return {};
    }

private:
    relayed_move             m_move;
    keyspace                &m_building;
    const schema_registry   &m_schema;
    const keyed_merge_state &m_keyed;

    // Shallowest first: renumbering an outer container invalidates inner paths.
    expected<std::vector<key_path>, resolve_fold_error>
    ordered_relayed_scopes(const std::set<std::string> &scopes) const
    {
        std::vector<key_path> declared;
        declared.reserve(scopes.size());
        for(const std::string &scope : scopes)
        {
            auto parsed = key_path::parse(scope);
            if(!parsed)
                return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: declared scope failed to parse "
                                                                                "in compact_relayed_instances(): '{}'",
                                                                                scope)});
            declared.push_back(std::move(parsed).value());
        }
        std::sort(declared.begin(), declared.end(),
                  [](const key_path &a, const key_path &b)
                  {
                      return a.size() != b.size() ? a.size() < b.size()
                                                  : a.str() < b.str();
                  });
        return declared;
    }

    std::vector<std::string> building_key_texts() const
    {
        std::vector<std::string> keys;
        keys.reserve(m_building.size());
        for(const key_path &path : m_building.paths())
            keys.push_back(path.str());
        return keys;
    }

    expected<relayed_instance_groups, resolve_fold_error>
    grouped_relayed_instances(const key_path &declared) const
    {
        const std::vector<std::string> keys = building_key_texts();
        relayed_instance_groups        groups;
        for(const std::string &text : instances_of(m_schema, keys, declared))
        {
            auto actual = key_path::parse(text);
            if(!actual)
                return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: enumerated instance failed to parse "
                                                                                "in compact_relayed_instances(): '{}'",
                                                                                text)});
            const std::uint64_t ordinal = key_path::ordinal_of(actual->leaf());

            std::vector<std::pair<std::size_t, key_path>> &group = groups[actual->parent().str()];
            group.emplace_back(static_cast<std::size_t>(ordinal), std::move(actual).value());
        }
        for(auto &[_, instances] : groups)
            std::sort(instances.begin(), instances.end(),
                      [](const auto &a, const auto &b)
                      { return a.first < b.first; });
        return groups;
    }

    expected<void, resolve_fold_error>
    compact_relayed_scope(const key_path &declared)
    {
        auto groups = grouped_relayed_instances(declared);
        if(!groups)
            return unexpected(std::move(groups).error());
        for(const auto &[_, instances] : groups.value())
        {
            std::size_t target_ordinal = 0;
            for(const auto &[source_ordinal, source] : instances)
            {
                if(source_ordinal != target_ordinal)
                {
                    const auto segment = std::string(key_path::base_name(source.leaf())) +
                            "[" + std::to_string(target_ordinal) + "]";
                    if(auto moved = m_move.compact_relayed_instance(
                               source, source.parent().child(segment));
                       !moved)
                        return moved;
                }
                ++target_ordinal;
            }
        }
        return {};
    }
};

}

#endif
