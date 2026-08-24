#ifndef HPP_GUARD_NUCLEUS_SCHEMA_KEYREF_PASS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_KEYREF_PASS_H

#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/schema_validation.h"
#include "nucleus/schema/keyref_candidate_index.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/config.h"
#include "nucleus/format.h"

#include <span>
#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace nucleus {

class keyref_pass
{
public:
    static void check_keyref(const schema_registry &schema, const config &resolved,
                             const schema_element          &el,
                             std::vector<schema_violation> &out)
    {
        const identity_group_spec *group = group_of(schema, el);
        if(group == nullptr)
            return;
        const keyref_candidate_index index(resolved.root(), *group,
                                           [&schema](std::string_view path)
                                           { return canonicalize(schema, path); });
        const std::string            declared = el.declared_path().str();
        for(const std::string &key : resolved.keys())
            check_key(schema, resolved, *group, index, declared, key, out);
    }

private:
    static std::string canonicalize(const schema_registry &schema, std::string_view path)
    {
        const auto parsed = key_path::parse(path);
        return parsed ? schema.canonical_text(*parsed) : std::string{};
    }

    static const identity_group_spec *
    group_of(const schema_registry &schema, const schema_element &el)
    {
        for(const identity_group_spec &group : schema.identity_groups())
            if(group.name == el.keyref_into)
                return &group;
        return nullptr;
    }

    static void check_key(const schema_registry &schema, const config &resolved,
                          const identity_group_spec    &group,
                          const keyref_candidate_index &index,
                          const std::string &declared, const std::string &key,
                          std::vector<schema_violation> &out)
    {
        const auto path = key_path::parse(key);
        if(!path || schema.canonical_text(*path) != declared)
            return;
        const auto value = resolved.get(key);
        if(!value)
            return;
        const auto result = index.find(config_node{&resolved, key}, *value);
        if(result.matches.size() == 1)
            return;
        if(result.matches.empty())
            report_missing(group, key, *value, result, out);
        else
            report_ambiguous(group, key, *value, result, out);
    }

    static void report_missing(const identity_group_spec &group, const std::string &path,
                               const std::string &value, const keyref_candidate_result &result,
                               std::vector<schema_violation> &out)
    {
        std::string reason = nucleus::format(
                "keyref '{}'='{}' matches no identifier (0 targets) in namespace '{}' "
                "within qualified scope '{}'",
                path, value, group.name, result.qualified_scope);
        const std::span<const std::string> candidates(result.values.data(), result.values.size());
        if(const auto near = suggest_keys(value, candidates, 1); !near.empty())
            reason += nucleus::format(" (did you mean '{}'?)", near.front());
        out.push_back(schema_violation{path, std::move(reason)});
    }

    static void report_ambiguous(const identity_group_spec &group, const std::string &path,
                                 const std::string &value, const keyref_candidate_result &result,
                                 std::vector<schema_violation> &out)
    {
        out.push_back(schema_violation{path, nucleus::format("keyref '{}'='{}' matches {} targets in namespace '{}' within qualified scope '{}'", path, value, result.matches.size(), group.name, result.qualified_scope)});
    }
};

}

#endif
