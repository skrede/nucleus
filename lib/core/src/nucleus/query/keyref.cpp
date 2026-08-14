#include "nucleus/query/query.h"

#include "nucleus/schema/keyref_candidate_index.h"

#include "nucleus/format.h"

#include <string>
#include <string_view>

namespace nucleus {

namespace {

config_node root_of(config_node node)
{
    while(!node.path().empty())
        node = node.parent();
    return node;
}

error missing_error(const std::string &path, const std::string &value,
                    const identity_group_spec &group, const std::string &scope)
{
    return error{errc::absent_key, nucleus::format("keyref '{}'='{}' matches no identifier (0 targets) in namespace '{}' "
                                                   "within qualified scope '{}'",
                                                   path, value, group.name, scope)};
}

error ambiguous_error(const std::string &path, const std::string &value,
                      const identity_group_spec &group, const keyref_candidate_result &result)
{
    return error{errc::ambiguous_result, nucleus::format("keyref '{}'='{}' matches {} targets in namespace '{}' within qualified scope '{}'", path, value, result.matches.size(), group.name, result.qualified_scope)};
}

}

expected<config_node, error>
follow_keyref(const config_node &keyref_leaf, const schema_query_context &ctx)
{
    const std::string path(keyref_leaf.path());
    const auto        value = keyref_leaf.value();
    if(!value)
        return unexpected(error{errc::absent_key,
                                "keyref node '" + path + "' has no value to follow"});
    const identity_group_spec *group = ctx.keyref_target(ctx.canonicalize(path));
    if(group == nullptr)
        return unexpected(error{errc::absent_key,
                                "node '" + path + "' is not a declared keyref"});
    const keyref_candidate_index  index(root_of(keyref_leaf), *group,
                                        [&ctx](std::string_view candidate)
                                        { return ctx.canonicalize(candidate); });
    const keyref_candidate_result result = index.find(keyref_leaf, *value);
    if(result.matches.empty())
        return unexpected(missing_error(path, *value, *group, result.qualified_scope));
    if(result.matches.size() > 1)
        return unexpected(ambiguous_error(path, *value, *group, result));
    return result.matches.front();
}

}
