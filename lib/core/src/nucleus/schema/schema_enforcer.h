#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H

#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string_view>
#include <utility>
#include <variant>

namespace nucleus {

// One validation failure: the path it concerns and a human-readable reason. A
// validation run collects every violation rather than aborting on the first, so a
// host sees the whole picture in one pass.
struct schema_violation
{
    std::string path;
    std::string reason;
};

using schema_validation = expected<void, std::vector<schema_violation>>;

// Validates a resolved keyspace against the registered schema -- the step that
// makes the schema authoritative over CONTENT, not just shape. Four independent
// checks, mirroring four distinct concepts:
//
//   * unknown paths   -- every value in the keyspace must correspond to a
//                        declared element (the schema is the single authority;
//                        a value at an undeclared path is rejected).
//   * required        -- every element marked required must carry a value, checked
//                        per concrete instance of its innermost repeated ancestor
//                        so a populated sibling cannot vouch for an empty one. The
//                        identity (primary-key) element imposes NO presence
//                        obligation of its own: anonymous (keyless) strains are
//                        legal and collapse into the config space.
//                        Marking the identity element required is the host's
//                        knob for demanding a NAMED strain -- a sliced strain
//                        satisfies it structurally (the key value named the
//                        instance and was consumed), anonymous-only content
//                        violates it.
//   * allowed values  -- when an element declares a closed value set, a present
//                        value outside that set is rejected; the nearest allowed
//                        value (by edit distance) is offered as a suggestion.
//   * unique          -- a non-identity `unique` leaf's value must be distinct
//                        within one concrete instance of the repeated scope
//                        enclosing its container, not across unrelated outer
//                        instances.
//
// The enforcer borrows the schema and the keyspace as parameters -- it stores
// neither and holds no registry reference, in keeping with the flat topology.
class schema_enforcer
{
    // The inputs every pass borrows, computed once per run: the resolved paths and
    // their text forms materialized so the keyspace is parsed once rather than once
    // per declared element, and the declared repeated paths the instance
    // enumeration tests against.
    struct pass_input
    {
        const keyspace &resolved;
        std::vector<key_path> paths;
        std::vector<std::string> keys;
        const schema_registry &schema;
        std::set<std::string> repeated_declared;
    };

public:
    // `keyed_satisfied` carries the container paths whose primary-keyed instance
    // the resolve boundary already sliced onto the unified hierarchy: their
    // identity field was consumed (its value named the instance, then the
    // transient segment was stripped), so it is satisfied without appearing as a
    // leaf. Callers validating a raw keyspace pass nothing.
    static schema_validation
    validate(const schema_registry &schema, const keyspace &resolved,
             const std::vector<std::string> &keyed_satisfied = {})
    {
        const pass_input in = materialize(schema, resolved);

        std::vector<schema_violation> violations;
        check_unknown_paths(in, violations);
        for(const schema_element &el : schema.elements())
        {
            check_required(in, el, keyed_satisfied, violations);
            check_allowed_values(in, el, violations);
        }
        for(const schema_element &el : schema.elements())
            check_unique(in, el, violations);

        if(!violations.empty())
            return unexpected(std::move(violations));
        return {};
    }

private:
    static pass_input materialize(const schema_registry &schema, const keyspace &resolved)
    {
        std::vector<key_path> paths = resolved.paths();
        std::vector<std::string> keys;
        keys.reserve(paths.size());
        for(const key_path &path : paths)
            keys.push_back(path.str());
        return {resolved, std::move(paths), std::move(keys), schema,
                repeated_declared_paths(schema)};
    }

    // The schema is the authority over what may exist. An indexed path
    // (cluster/node[0]/port) is validated against its canonical form
    // (cluster/node/port) so repeated-container instances pass the gate.
    static void check_unknown_paths(const pass_input &in,
                                    std::vector<schema_violation> &out)
    {
        for(const key_path &path : in.paths)
        {
            const std::string canonical = in.schema.canonical_text(path);
            if(!in.schema.recognizes_text(canonical))
                out.push_back(schema_violation{path.str(), nucleus::format(
                    "path '{}' is not declared by the schema", path.str())});
        }
    }

    static schema_violation missing(const std::string &path)
    {
        return {path, nucleus::format("required field '{}' is missing", path)};
    }

    // Presence at the declared path itself: a value written there, or any concrete
    // instance path whose canonical form is it.
    static bool present_at(const pass_input &in, const std::string &declared)
    {
        for(const key_path &path : in.paths)
        {
            if(path.str() == declared || in.schema.canonical_text(path) == declared)
                return true;
        }
        return false;
    }

    // Presence, checked against the concrete instances of the element's innermost
    // repeated ancestor rather than pooled across them: a sibling instance that
    // supplies the value no longer satisfies the ones that do not. Without such an
    // ancestor there is no per-instance dimension and the declared path is checked
    // directly. A sliced strain satisfies a required identity element structurally
    // -- its key value named the instance and was consumed, so no leaf can exist.
    static void
    check_required(const pass_input &in, const schema_element &el,
                   const std::vector<std::string> &keyed_satisfied,
                   std::vector<schema_violation> &out)
    {
        if(!el.required)
            return;
        const std::string container = el.container().str();
        if(el.identity && std::ranges::find(keyed_satisfied, container)
                              != keyed_satisfied.end())
            return;
        const std::string declared = el.declared_path().str();
        const std::string scope = repeated_scope_of(in.repeated_declared, container);
        if(scope.empty())
        {
            if(!present_at(in, declared))
                out.push_back(missing(declared));
            return;
        }
        require_per_instance(in, declared, scope, out);
    }

    // One violation per concrete instance of `scope` whose member carries no value.
    // Zero instances emits nothing: whether an empty repeated container is legal is
    // decided by that container element's own required flag, which the same element
    // loop checks. The member test matches an indexed instance too, which is the
    // storage shape of a repeated member.
    static void
    require_per_instance(const pass_input &in, const std::string &declared,
                         const std::string &scope, std::vector<schema_violation> &out)
    {
        const auto scope_path = key_path::parse(scope);
        if(!scope_path)
            return;
        const std::string relative = declared.substr(scope.size() + 1);
        for(const std::string &instance : instances_of(in.schema, in.keys, *scope_path))
        {
            const std::string member = join_segment(instance, relative);
            const bool carried = std::ranges::any_of(in.keys,
                [&](const std::string &key) { return names_member_instance(key, member); });
            if(!carried)
                out.push_back(missing(member));
        }
    }

    static void check_value(const schema_element &el, const std::string &instance_path,
                            const std::string &actual, std::vector<schema_violation> &out)
    {
        const bool admissible = std::any_of(
            el.allowed_values.begin(), el.allowed_values.end(),
            [&](const std::string &a) { return a == actual; });
        if(admissible)
            return;
        std::string reason = nucleus::format(
            "field '{}' value '{}' is not one of the allowed values",
            instance_path, actual);
        const std::span<const std::string> candidates(el.allowed_values.data(),
                                                      el.allowed_values.size());
        if(auto nearest = suggest_keys(actual, candidates, 1); !nearest.empty())
            reason += nucleus::format(" (did you mean '{}'?)", nearest.front());
        out.push_back(schema_violation{instance_path, std::move(reason)});
    }

    // Every concrete instance value must be one of the declared allowed values. An
    // unconstrained element (empty set) skips this. A non-repeated leaf under a
    // repeated or keyed container carries no value at the plain declared path, so
    // the indexed instances are matched canonically.
    static void check_allowed_values(const pass_input &in, const schema_element &el,
                                     std::vector<schema_violation> &out)
    {
        if(el.allowed_values.empty())
            return;
        const key_path declared = el.declared_path();
        if(const value *direct = in.resolved.find(declared))
        {
            check_value(el, declared.str(), std::string(direct->text()), out);
            return;
        }
        for(const key_path &path : in.paths)
        {
            if(in.schema.canonical_text(path) != declared.str())
                continue;
            if(const value *v = in.resolved.find(path))
                check_value(el, path.str(), std::string(v->text()), out);
        }
    }

    static void report_duplicates(const std::string &declared, const std::string &text,
                                  const std::vector<std::string> &instances,
                                  std::vector<schema_violation> &out)
    {
        if(instances.size() <= 1)
            return;
        std::string parties;
        for(std::size_t i = 0; i < instances.size(); ++i)
        {
            if(i)
                parties += ", ";
            parties += nucleus::format("'{}'", instances[i]);
        }
        out.push_back(schema_violation{instances.front(), nucleus::format(
            "unique field '{}' has duplicate value '{}' across sibling instances {}",
            declared, text, parties)});
    }

    // A non-identity `unique` value competes only within one concrete instance of
    // the innermost repeated scope STRICTLY above its container -- two nodes may
    // each carry a route on port 8080, two routes of one node may not; scoping to
    // the container itself would pool each innermost instance alone and leave the
    // check unfireable. A keyed (pkey) container is reconciled to one surviving
    // strain by slice time, so this pass cannot collide with the slice-time check.
    static void check_unique(const pass_input &in, const schema_element &el,
                             std::vector<schema_violation> &out)
    {
        if(!el.unique || el.identity)
            return;
        const std::string declared = el.declared_path().str();
        const std::string scope =
            repeated_scope_of(in.repeated_declared, el.container().parent().str());
        std::map<std::pair<std::string, std::string>, std::vector<std::string>> pools;
        for(const key_path &path : in.paths)
        {
            if(in.schema.canonical_text(path) != declared)
                continue;
            if(const value *v = in.resolved.find(path))
                pools[{pool_of(in, path, scope), std::string(v->text())}]
                    .push_back(path.str());
        }
        for(const auto &[pool, instances] : pools)
            report_duplicates(declared, pool.second, instances, out);
    }

    static std::string pool_of(const pass_input &in, const key_path &path,
                               const std::string &scope)
    {
        if(scope.empty())
            return {};
        return instance_prefix(in.schema, path, scope);
    }
};

}

#endif
