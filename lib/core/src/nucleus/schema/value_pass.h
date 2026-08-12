#ifndef HPP_GUARD_NUCLEUS_SCHEMA_VALUE_PASS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_VALUE_PASS_H

#include "nucleus/format.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"

#include <map>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>

namespace nucleus {

class value_pass
{
public:
    // Every concrete instance value must be one of the declared allowed values. An
    // unconstrained element (empty set) skips this. A non-repeated leaf under a
    // repeated or keyed container carries no value at the plain declared path, so
    // the indexed instances are matched canonically.
    static void check_allowed_values(const validation_input &in, const schema_element &el,
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

    // A non-identity `unique` value competes only within one concrete instance of
    // the innermost repeated scope STRICTLY above its container -- two nodes may
    // each carry a route on port 8080, two routes of one node may not; scoping to
    // the container itself would pool each innermost instance alone and leave the
    // check unfireable. A keyed (pkey) container is reconciled to one surviving
    // strain by slice time, so this pass cannot collide with the slice-time check.
    static void check_unique(const validation_input &in, const schema_element &el,
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

private:
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

    static std::string pool_of(const validation_input &in, const key_path &path,
                               const std::string &scope)
    {
        if(scope.empty())
            return {};
        return instance_prefix(in.schema, path, scope);
    }
};

}

#endif
