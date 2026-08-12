#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H

#include "nucleus/expected.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/value_pass.h"
#include "nucleus/schema/presence_pass.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

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
        const validation_input in = materialize(schema, resolved);

        std::vector<schema_violation> violations;
        presence_pass::check_unknown_paths(in, violations);
        for(const schema_element &el : schema.elements())
        {
            presence_pass::check_required(in, el, keyed_satisfied, violations);
            value_pass::check_allowed_values(in, el, violations);
        }
        for(const schema_element &el : schema.elements())
            value_pass::check_unique(in, el, violations);

        if(!violations.empty())
            return unexpected(std::move(violations));
        return {};
    }

private:
    static validation_input materialize(const schema_registry &schema,
                                        const keyspace &resolved)
    {
        std::vector<key_path> paths = resolved.paths();
        std::vector<std::string> keys;
        keys.reserve(paths.size());
        for(const key_path &path : paths)
            keys.push_back(path.str());
        return {resolved, std::move(paths), std::move(keys), schema,
                repeated_declared_paths(schema)};
    }
};

}

#endif
