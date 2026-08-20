#ifndef HPP_GUARD_NUCLEUS_RESOLVE_SCHEMA_GATE_H
#define HPP_GUARD_NUCLEUS_RESOLVE_SCHEMA_GATE_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/storage_shape.h"

#include "nucleus/schema/group_enforcer.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/diagnostics/key_suggester.h"

#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace nucleus {

// Produces the transient config the group pass walks. Injected rather than
// built here: config's snapshot constructor is private and the resolution
// context is its only friend, so the gate cannot mint one itself.
using config_snapshot_fn = std::function<config()>;

// The content gate: the step that makes the schema authoritative over VALUES at
// resolve time. It BORROWS the folded keyspace, the schema it gates against, the
// sweep it canonicalizes a violation path through, and the record of which keyed
// containers were satisfied structurally by the slice.
class schema_gate
{
public:
    schema_gate(const keyspace &building, const schema_registry &schema,
                const repeated_sweep &sweep,
                const std::vector<std::string> &keyed_satisfied,
                config_snapshot_fn snapshot) noexcept
        : m_building(building)
        , m_schema(schema)
        , m_sweep(sweep)
        , m_keyed_satisfied(keyed_satisfied)
        , m_snapshot_cb(std::move(snapshot))
    {
    }

    // The unknown-path and required-field checks run ONLY when the schema
    // declares a surface: a host that registers no schema gets no content gate,
    // because an empty schema is not a claim that nothing is allowed. The group
    // pass is orthogonal and runs whenever any group is registered -- a
    // root-anchored host-validator valve carries no element surface yet must
    // still enforce.
    expected<void, resolve_fold_error> validate() const
    {
        const std::vector<key_path> paths = m_building.paths();
        if(auto shape = validate_storage_shape(paths); !shape)
            return unexpected(std::move(shape).error());

        const bool has_groups = !m_schema.constraint_groups().empty()
                             || !m_schema.identity_groups().empty();
        if(m_schema.surface().empty() && !has_groups)
            return {};

        const schema_validation checked = m_schema.surface().empty()
            ? schema_validation{}
            : schema_enforcer::validate(m_schema, m_building, m_keyed_satisfied);
        const std::vector<schema_violation> grouped = group_violations(has_groups);
        if(checked && grouped.empty())
            return {};
        return unexpected(error{errc::schema_violation, report(checked, grouped)});
    }

private:
    // Container-scoped constraint and identity groups enforce over the resolved,
    // sliced tree, on a transient config snapshot so the host-validator valve and
    // member navigation use the real config_node walk. Skipped when no group is
    // declared, so the common case pays nothing.
    std::vector<schema_violation> group_violations(bool has_groups) const
    {
        if(!has_groups)
            return {};
        const config snapshot = m_snapshot_cb();
        return group_enforcer::validate(m_schema, snapshot);
    }

    std::string report(const schema_validation &checked,
                       const std::vector<schema_violation> &grouped) const
    {
        std::string text = "schema validation failed:";
        if(!checked)
        {
            const std::vector<std::string> known = declared_paths();
            for(const schema_violation &v : checked.error())
                text += nucleus::format("\n  - {}{}", v.reason,
                                        suggestion(v.path, known));
        }
        for(const schema_violation &v : grouped)
            text += nucleus::format("\n  - {}", v.reason);
        return text;
    }

    // An unknown path gets a did-you-mean. A violation naming a path the schema
    // already recognizes draws none, and neither does a group violation, which
    // names its parties precisely.
    std::string suggestion(const std::string &path,
                           const std::vector<std::string> &known) const
    {
        if(m_schema.recognizes_text(m_sweep.canonical_of(path)))
            return {};
        const std::vector<std::string> near = suggest_keys(path, known, 1);
        if(near.empty())
            return {};
        return nucleus::format(" (did you mean '{}'?)", near.front());
    }

    std::vector<std::string> declared_paths() const
    {
        const std::vector<key_path> surface = m_schema.surface();
        std::vector<std::string> known;
        known.reserve(surface.size());
        for(const key_path &path : surface)
            known.push_back(path.str());
        return known;
    }

    const keyspace                 &m_building;
    const schema_registry          &m_schema;
    const repeated_sweep           &m_sweep;
    const std::vector<std::string> &m_keyed_satisfied;
    config_snapshot_fn              m_snapshot_cb;
};

}

#endif
