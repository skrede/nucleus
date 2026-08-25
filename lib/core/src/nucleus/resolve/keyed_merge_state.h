#ifndef HPP_GUARD_NUCLEUS_RESOLVE_KEYED_MERGE_STATE_H
#define HPP_GUARD_NUCLEUS_RESOLVE_KEYED_MERGE_STATE_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/schema_registry.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// Owns the keyed-composition state of one resolve: which repeated containers
// declare a non-default merge mode, the identity-group field keying each of them,
// the declared leaf names a flat arrival is matched against, and the leaves
// diverted off the sweep path to await the merge. It owns all of that outright and
// borrows nothing; the schema is passed per call rather than stored, because only
// construction reads it. Lives exactly as long as the resolve that holds it.
class keyed_merge_state
{
public:
    // One leaf diverted out of the fold, tagged with the layer rank and the
    // per-layer instance ordinal that together identify the instance it belongs to.
    struct keyed_instance_entry
    {
        std::size_t source_rank;
        std::size_t source_ordinal;
        std::string suffix;
        std::string value;
        origin      prov;
    };

    // Records every repeated element declaring a non-default merge mode against the
    // identity-group field that keys it. A keyed mode with no covering identity
    // group has no merge key -- a loud error.
    expected<void, resolve_fold_error> build(const schema_registry &schema)
    {
        for(const schema_element &el : schema.elements())
        {
            if(!el.repeated || el.merge == merge_mode::replace_by_ordinal)
                continue;
            const std::string cpath = el.declared_path().str();
            const std::string field = merge_key_field(schema, cpath);
            if(field.empty())
                return unexpected(error{errc::schema_violation, nucleus::format(
                    "repeated element '{}' declares a keyed merge mode but no identity "
                    "group provides its merge key", cpath)});
            m_keyed_modes[cpath] = el.merge;
            m_keyed_fields[cpath] = field;
        }
        build_declared_suffixes(schema);
        return {};
    }

    bool any_declared() const noexcept { return !m_keyed_modes.empty(); }

    bool declares(const std::string &canonical) const { return m_keyed_modes.contains(canonical); }

    merge_mode mode_of(const std::string &canonical) const { return m_keyed_modes.at(canonical); }

    const std::string &field_of(const std::string &canonical) const { return m_keyed_fields.at(canonical); }

    const std::string &canonical_of_actual(const std::string &actual) const { return m_actual_to_canonical.at(actual); }

    std::map<std::string, std::vector<keyed_instance_entry>> &accumulator() noexcept { return m_keyed_accumulator; }

    const std::set<std::string> &declared_suffixes(const std::string &canonical) const
    {
        static const std::set<std::string> undeclared;
        const auto it = m_declared_suffixes.find(canonical);
        return it == m_declared_suffixes.end() ? undeclared : it->second;
    }

    void divert(const std::string &actual_container, const std::string &canonical,
                keyed_instance_entry &&entry)
    {
        m_actual_to_canonical[actual_container] = canonical;
        m_keyed_accumulator[actual_container].push_back(std::move(entry));
    }

    // True when a canonical path is at or under a container declaring a keyed merge
    // mode (unite/replace_by_key). Such collections are finalized across layers by
    // the keyed merge; the strain slice must relay them verbatim, never rank-prune.
    bool under_keyed_merge(const std::string &canonical_path) const
    {
        for(const auto &[cpath, mode] : m_keyed_modes)
        {
            const std::string cslash = cpath + key_path::separator;
            if(canonical_path == cpath
               || canonical_path.starts_with(cslash))
                return true;
        }
        return false;
    }

private:
    // Canonical container path -> its non-default merge mode, and the identity-group
    // field that keys it. The two maps carry the same key set by construction: a
    // keyed mode without a covering identity group is rejected, never recorded.
    std::map<std::string, merge_mode> m_keyed_modes;
    std::map<std::string, std::string> m_keyed_fields;
    // Declared leaf names per keyed-merge container, so field-major arrival is
    // detected from the schema rather than learned incrementally from the data --
    // see the flat grouping's own comment for why that distinction matters.
    std::map<std::string, std::set<std::string>> m_declared_suffixes;
    // ACTUAL container path (key segments still present pre-slice) -> diverted leaves,
    // and its canonical form (to look up mode/field).
    std::map<std::string, std::vector<keyed_instance_entry>> m_keyed_accumulator;
    std::map<std::string, std::string> m_actual_to_canonical;

    static std::string merge_key_field(const schema_registry &schema, const std::string &cpath)
    {
        for(const identity_group_spec &g : schema.identity_groups())
        {
            const std::string parent = g.container().str();
            for(const std::string &m : g.members)
            {
                std::string mp = parent;
                if(!mp.empty())
                    mp += key_path::separator;
                mp += m;
                if(mp == cpath)
                    return g.field;
            }
        }
        return {};
    }

    void build_declared_suffixes(const schema_registry &schema)
    {
        for(const auto &[cpath, mode] : m_keyed_modes)
        {
            const auto container_kp = key_path::parse(cpath);
            if(!container_kp)
                continue;
            std::set<std::string> &declared = m_declared_suffixes[cpath];
            for(const schema_element &child : schema.elements())
                if(child.container() == container_kp.value())
                    declared.insert(child.name);
        }
    }
};

}

#endif
