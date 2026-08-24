#ifndef HPP_GUARD_NUCLEUS_SCHEMA_KEYREF_CANDIDATE_INDEX_H
#define HPP_GUARD_NUCLEUS_SCHEMA_KEYREF_CANDIDATE_INDEX_H

#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/instance_paths.h"

#include "nucleus/config.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <string_view>

namespace nucleus {

struct keyref_candidate_result
{
    std::vector<config_node> matches;
    std::vector<std::string> values;
    std::string              qualified_scope;
};

class keyref_candidate_index
{
    struct candidate
    {
        config_node target;
        std::string value;
    };

public:
    using canonicalizer = std::function<std::string(std::string_view)>;

    keyref_candidate_index(const config_node &root, identity_group_spec group,
                           canonicalizer canonicalize)
            : m_group(std::move(group))
            , m_canonicalize_cb(std::move(canonicalize))
            , m_candidates()
    {
        collect(root);
    }

    keyref_candidate_result find(const config_node &reference, std::string_view value) const
    {
        const auto              path  = key_path::parse(reference.path());
        const std::string       scope = path
                ? qualified_scope(*path, m_group.container(), m_canonicalize_cb)
                : std::string{};
        keyref_candidate_result result{{}, {}, scope.empty() ? "<unbound>" : scope};
        for(const candidate &entry : m_candidates)
        {
            if(!within_scope(entry.target.path(), scope))
                continue;
            result.values.push_back(entry.value);
            if(entry.value == value)
                result.matches.push_back(entry.target);
        }
        return result;
    }

private:
    identity_group_spec    m_group;
    canonicalizer          m_canonicalize_cb;
    std::vector<candidate> m_candidates;

    void collect(const config_node &root)
    {
        root.visit([this](const config_node &node)
                   {
            collect_node(node);
            return true; });
    }

    void collect_node(const config_node &node)
    {
        if(node.kind() != node_kind::scalar)
            return;
        const auto value = node.value();
        if(!value || !is_identifier(m_canonicalize_cb(node.path())))
            return;
        m_candidates.push_back(candidate{node.parent(), *value});
    }

    bool is_identifier(std::string_view canonical) const
    {
        const std::string parent = m_group.container().str();
        for(const std::string &member : m_group.members)
        {
            const std::string target = join_segment(
                    join_segment(parent, member), m_group.field);
            if(canonical == target)
                return true;
        }
        return false;
    }

    static bool within_scope(std::string_view target, std::string_view scope)
    {
        if(scope.empty())
            return true;
        return target.size() > scope.size() && target.starts_with(scope) && target[scope.size()] == key_path::separator;
    }
};

}

#endif
