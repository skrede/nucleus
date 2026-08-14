#ifndef HPP_GUARD_NUCLEUS_SCHEMA_GROUP_ENFORCER_H
#define HPP_GUARD_NUCLEUS_SCHEMA_GROUP_ENFORCER_H

#include "nucleus/config.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/keyref_pass.h"
#include "nucleus/schema/identity_pass.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_pass.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/schema_validation.h"

#include <vector>

namespace nucleus {

// Enforces container-scoped constraint groups, identity groups and key references
// over the resolved, sliced configuration. Violations are collected rather than
// thrown. The enforcer borrows the schema and config and stores neither.
class group_enforcer
{
public:
    static std::vector<schema_violation>
    validate(const schema_registry &schema, const config &resolved)
    {
        std::vector<schema_violation> violations;
        for(const constraint_group &g : schema.constraint_groups())
            constraint_pass::check_constraint_group(schema, resolved, g, violations);
        for(const identity_group_spec &g : schema.identity_groups())
            identity_pass::check_identity_group(schema, resolved, g, violations);
        for(const schema_element &el : schema.elements())
            if(!el.keyref_into.empty())
                keyref_pass::check_keyref(schema, resolved, el, violations);
        return violations;
    }
};

}

#endif
