#ifndef HPP_GUARD_NUCLEUS_SCHEMA_IDENTITY_PASS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_IDENTITY_PASS_H

#include "nucleus/config.h"
#include "nucleus/format.h"

#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/keyspace/key_path.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

class identity_pass
{
    using identity_hit  = std::pair<std::string, std::string>;
    using identity_pool = std::map<std::pair<std::string, std::string>,
                                   std::vector<identity_hit>>;

public:
    // An identifier competes only within one concrete instance of the repeated
    // scope enclosing the group's container. Sibling instances therefore own
    // independent identity namespaces.
    static void check_identity_group(const schema_registry         &schema,
                                     const config                  &resolved,
                                     const identity_group_spec     &g,
                                     std::vector<schema_violation> &out)
    {
        const std::string           parent   = g.container().str();
        const std::set<std::string> repeated = repeated_declared_paths(schema);
        const std::string           scope    = repeated_scope_of(repeated, parent);
        identity_pool               pool;
        for(const std::string &m : g.members)
            collect_member(schema, resolved, g, parent, scope, m, pool, out);
        for(const auto &[key, hits] : pool)
            report_duplicate(g, key.second, hits, out);
    }

private:
    static std::vector<std::string>
    instances_of(const schema_registry &schema, const config &resolved,
                 const key_path &declared)
    {
        return nucleus::instances_of(schema, resolved.keys(), declared);
    }

    static void collect_member(const schema_registry     &schema,
                               const config              &resolved,
                               const identity_group_spec &g,
                               const std::string         &parent,
                               const std::string         &scope,
                               const std::string &m, identity_pool &pool,
                               std::vector<schema_violation> &out)
    {
        auto member_kp = key_path::parse(join_segment(parent, m));
        if(!member_kp)
            return;
        for(const std::string &mi : instances_of(schema, resolved, *member_kp))
            collect_instance(schema, resolved, g, scope, m, mi, pool, out);
    }

    static void collect_instance(const schema_registry     &schema,
                                 const config              &resolved,
                                 const identity_group_spec &g,
                                 const std::string         &scope,
                                 const std::string &m, const std::string &mi,
                                 identity_pool                 &pool,
                                 std::vector<schema_violation> &out)
    {
        const std::string field_path = join_segment(mi, g.field);
        auto              v          = resolved.get(field_path);
        if(!v.has_value())
        {
            out.push_back(schema_violation{mi, nucleus::format("identity group '{}': member '{}' instance '{}' is missing "
                                                               "its identifier field '{}'",
                                                               g.name, m, mi, g.field)});
            return;
        }
        const auto path = key_path::parse(field_path);
        if(path)
            pool[{pool_of(schema, *path, scope), *v}].emplace_back(m, field_path);
    }

    static void report_duplicate(const identity_group_spec       &g,
                                 const std::string               &value,
                                 const std::vector<identity_hit> &hits,
                                 std::vector<schema_violation>   &out)
    {
        if(hits.size() <= 1)
            return;
        std::string parties;
        for(std::size_t i = 0; i < hits.size(); ++i)
        {
            if(i)
                parties += ", ";
            parties += nucleus::format("'{}' (element-type '{}')",
                                       hits[i].second, hits[i].first);
        }
        out.push_back(schema_violation{hits.front().second, nucleus::format("identity group '{}': identifier '{}'='{}' is not unique within the "
                                                                            "slice -- declared by {}",
                                                                            g.name, g.field, value, parties)});
    }

    static std::string pool_of(const schema_registry &schema, const key_path &path,
                               const std::string &scope)
    {
        if(scope.empty())
            return {};
        return instance_prefix(schema, path, scope);
    }
};

}

#endif
