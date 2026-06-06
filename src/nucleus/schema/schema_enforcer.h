#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ENFORCER_H

#include "nucleus/result.h"
#include "nucleus/format.h"
#include "nucleus/schema/schema.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/schema/schema_registry.h"

#include <string>
#include <vector>
#include <variant>
#include <utility>

namespace nucleus {

// One validation failure: the path it concerns and a human-readable reason. A
// validation run collects every violation rather than aborting on the first, so a
// host sees the whole picture in one pass.
struct schema_violation
{
    std::string path;
    std::string reason;
};

using schema_validation = result<std::monostate, std::vector<schema_violation>>;

// Validates a resolved keyspace against the registered schema -- the step that
// makes the schema authoritative over CONTENT, not just shape. Three independent
// checks, mirroring the three distinct concepts:
//
//   * unknown paths   -- every value in the keyspace must correspond to a
//                        declared element (the schema is the single authority;
//                        a value at an undeclared path is rejected).
//   * required        -- every element marked required must carry a value.
//   * identity        -- every element marked identity (selector/primary key)
//                        must carry a value so the node it identifies is
//                        addressable. Checked SEPARATELY from required: an
//                        identity is validated for its selector role even when
//                        the host did not also mark it required.
//
// The enforcer borrows the schema and the keyspace as parameters -- it stores
// neither and holds no registry reference, in keeping with the flat topology.
class schema_enforcer
{
public:
    [[nodiscard]] static schema_validation validate(const schema_registry &schema,
                                                    const keyspace &resolved)
    {
        std::vector<schema_violation> violations;

        // Unknown-path check: the schema is the authority over what may exist.
        for(const key_path &path : resolved.paths())
        {
            if(!schema.recognizes(path))
            {
                violations.push_back(schema_violation{
                    path.str(),
                    ::nucleus::format("path '{}' is not declared by the schema",
                                      path.str())});
            }
        }

        // Required and identity checks: walked per declared element, kept as two
        // separate constraints so neither is implied by the other.
        for(const schema_element &el : schema.elements())
        {
            const key_path declared = el.declared_path();
            const bool present = resolved.contains(declared);

            if(el.required && !present)
            {
                violations.push_back(schema_violation{
                    declared.str(),
                    ::nucleus::format("required field '{}' is missing",
                                      declared.str())});
            }

            if(el.identity && !present)
            {
                violations.push_back(schema_violation{
                    declared.str(),
                    ::nucleus::format("identity field '{}' has no value to select on",
                                      declared.str())});
            }
        }

        if(!violations.empty())
            return fail(std::move(violations));
        return std::monostate{};
    }
};

}

#endif
