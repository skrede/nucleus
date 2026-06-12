#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H

#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <span>
#include <string>
#include <vector>
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
// makes the schema authoritative over CONTENT, not just shape. Three independent
// checks, mirroring the three distinct concepts:
//
//   * unknown paths   -- every value in the keyspace must correspond to a
//                        declared element (the schema is the single authority;
//                        a value at an undeclared path is rejected).
//   * required        -- every element marked required must carry a value. The
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
//
// The enforcer borrows the schema and the keyspace as parameters -- it stores
// neither and holds no registry reference, in keeping with the flat topology.
class schema_enforcer
{
public:
    // `keyed_satisfied` carries the container paths whose primary-keyed instance
    // the resolve boundary already sliced onto the unified hierarchy: their
    // identity field was consumed (its value named the instance, then the
    // transient segment was stripped), so it is satisfied without appearing as a
    // leaf. Callers validating a raw keyspace pass nothing.
    [[nodiscard]] static schema_validation
    validate(const schema_registry &schema, const keyspace &resolved,
             const std::vector<std::string> &keyed_satisfied = {})
    {
        std::vector<schema_violation> violations;

        // Unknown-path check: the schema is the authority over what may exist.
        for(const key_path &path : resolved.paths())
        {
            if(!schema.recognizes(path))
            {
                violations.push_back(schema_violation{
                    path.str(),
                    nucleus::format("path '{}' is not declared by the schema",
                                      path.str())});
            }
        }

        // Required check: walked per declared element. A sliced strain satisfies
        // a required identity element structurally -- its key value named the
        // instance and was consumed, so no literal leaf can exist.
        for(const schema_element &el : schema.elements())
        {
            const key_path declared = el.declared_path();
            const bool present = resolved.contains(declared);
            const bool keyed_ok = el.identity
                && std::ranges::find(keyed_satisfied, el.container().str())
                       != keyed_satisfied.end();

            if(el.required && !present && !keyed_ok)
            {
                violations.push_back(schema_violation{
                    declared.str(),
                    nucleus::format("required field '{}' is missing",
                                      declared.str())});
            }

            // Closed-value check: a present value must be one of the declared
            // allowed values. An unconstrained element (empty set) skips this.
            // For repeated elements, the check applies to EACH collection value.
            if(!el.allowed_values.empty() && resolved.contains(declared))
            {
                if(el.repeated)
                {
                    const std::vector<value> *col = resolved.find_collection(declared);
                    if(col)
                    {
                        for(const value &v : *col)
                        {
                            const std::string actual(v.text());
                            const bool admissible = std::any_of(
                                el.allowed_values.begin(), el.allowed_values.end(),
                                [&](const std::string &a) { return a == actual; });
                            if(!admissible)
                            {
                                std::string reason = nucleus::format(
                                    "field '{}' collection value '{}' is not one "
                                    "of the allowed values",
                                    declared.str(), actual);
                                const std::span<const std::string> candidates(
                                    el.allowed_values.data(),
                                    el.allowed_values.size());
                                auto nearest = suggest_keys(actual, candidates, 1);
                                if(!nearest.empty())
                                    reason += nucleus::format(
                                        " (did you mean '{}'?)", nearest.front());
                                violations.push_back(schema_violation{
                                    declared.str(), std::move(reason)});
                            }
                        }
                    }
                }
                else
                {
                    const value *v = resolved.find(declared);
                    const std::string actual(v ? v->text() : std::string_view{});
                    const bool admissible = std::any_of(
                        el.allowed_values.begin(), el.allowed_values.end(),
                        [&](const std::string &a) { return a == actual; });
                    if(!admissible)
                    {
                        std::string reason = nucleus::format(
                            "field '{}' value '{}' is not one of the allowed values",
                            declared.str(), actual);
                        const std::span<const std::string> candidates(
                            el.allowed_values.data(), el.allowed_values.size());
                        auto nearest = suggest_keys(actual, candidates, 1);
                        if(!nearest.empty())
                            reason += nucleus::format(" (did you mean '{}'?)",
                                                      nearest.front());
                        violations.push_back(schema_violation{declared.str(),
                                                              std::move(reason)});
                    }
                }
            }
        }

        if(!violations.empty())
            return unexpected(std::move(violations));
        return {};
    }
};

}

#endif
