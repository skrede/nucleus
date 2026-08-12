#ifndef HPP_GUARD_NUCLEUS_SCHEMA_PRESENCE_PASS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_PRESENCE_PASS_H

#include "nucleus/format.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <algorithm>

namespace nucleus {

class presence_pass
{
public:
    // The schema is the authority over what may exist. An indexed path
    // (cluster/node[0]/port) is validated against its canonical form
    // (cluster/node/port) so repeated-container instances pass the gate.
    static void check_unknown_paths(const validation_input &in,
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

    // Presence, checked against the concrete instances of the element's innermost
    // repeated ancestor rather than pooled across them: a sibling instance that
    // supplies the value no longer satisfies the ones that do not. Without such an
    // ancestor there is no per-instance dimension and the declared path is checked
    // directly. A sliced strain satisfies a required identity element structurally
    // -- its key value named the instance and was consumed, so no leaf can exist.
    static void
    check_required(const validation_input &in, const schema_element &el,
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

private:
    static schema_violation missing(const std::string &path)
    {
        return {path, nucleus::format("required field '{}' is missing", path)};
    }

    // Presence at the declared path itself: a value written there, or any concrete
    // instance path whose canonical form is it.
    static bool present_at(const validation_input &in, const std::string &declared)
    {
        for(const key_path &path : in.paths)
        {
            if(path.str() == declared || in.schema.canonical_text(path) == declared)
                return true;
        }
        return false;
    }

    // One violation per concrete instance of `scope` whose member carries no value.
    // Zero instances emits nothing: whether an empty repeated container is legal is
    // decided by that container element's own required flag, which the same element
    // loop checks. The member test matches an indexed instance too, which is the
    // storage shape of a repeated member.
    static void
    require_per_instance(const validation_input &in, const std::string &declared,
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
};

}

#endif
