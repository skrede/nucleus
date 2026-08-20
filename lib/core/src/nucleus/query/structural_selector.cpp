#include "nucleus/query/query.h"

#include "nucleus/config.h"

#include "nucleus/keyspace/key_path.h"

#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

namespace {

void append_unique(std::vector<config_node> &nodes, std::set<std::string> &seen,
                   config_node node)
{
    if(node.exists() && seen.insert(std::string(node.path())).second)
        nodes.push_back(std::move(node));
}

std::vector<config_node> navigation_bases(const std::vector<config_node> &roots)
{
    std::vector<config_node> result;
    std::set<std::string>    seen;
    for(const config_node &root : roots)
        if(root.kind() == node_kind::repeated)
            for(config_node child : root.children())
                append_unique(result, seen, std::move(child));
        else
            append_unique(result, seen, root);
    return result;
}

config_node select_segment(const config_node &base, const std::string &segment)
{
    config_node selected = base[std::string(key_path::base_name(segment))];
    if(key_path::is_indexed_segment(segment))
    {
        const std::uint64_t ordinal = key_path::ordinal_of(segment);
        selected                    = selected[static_cast<std::size_t>(ordinal)];
    }
    return selected;
}

std::vector<config_node> follow_segment(const std::vector<config_node> &roots,
                                        const std::string              &segment)
{
    std::vector<config_node> result;
    std::set<std::string>    seen;
    for(const config_node &base : navigation_bases(roots))
        append_unique(result, seen, select_segment(base, segment));
    return result;
}

std::vector<config_node> resolve_roots(const std::vector<config_node> &roots,
                                       std::string_view                subpath)
{
    if(subpath.empty())
        return roots;
    const auto parsed = key_path::parse(subpath);
    if(!parsed)
        return {};
    std::vector<config_node> current = roots;
    for(const std::string &segment : parsed->segments())
        current = follow_segment(current, segment);
    return current;
}

std::vector<std::string> paths_of(const std::vector<config_node> &nodes)
{
    std::vector<std::string> result;
    result.reserve(nodes.size());
    for(const config_node &node : nodes)
        result.emplace_back(node.path());
    return result;
}

bool in_subtree(std::string_view path, std::string_view root)
{
    if(root.empty() || path == root)
        return true;
    return path.size() > root.size() && path.starts_with(root) && (path[root.size()] == '/' || path[root.size()] == '[');
}

std::size_t observable_depth(std::string_view path)
{
    const auto parsed = key_path::parse(path);
    if(!parsed)
        return 0;
    std::size_t depth = parsed->segments().size();
    for(const std::string &segment : parsed->segments())
        depth += key_path::is_indexed_segment(segment) ? 1U : 0U;
    return depth;
}

bool at_relative_depth(std::string_view path, std::string_view root, std::size_t depth)
{
    return in_subtree(path, root) && observable_depth(path) == observable_depth(root) + depth;
}

bool matches_depth(std::string_view path, const std::vector<std::string> &roots,
                   std::size_t depth)
{
    for(const std::string &root : roots)
        if(at_relative_depth(path, root, depth))
            return true;
    return false;
}

bool matches_subtree(std::string_view path, const std::vector<std::string> &roots,
                     bool include_roots)
{
    for(const std::string &root : roots)
        if(in_subtree(path, root) && (include_roots || path != root))
            return true;
    return false;
}

}

selector selector::children() const
{
    return at_depth(1);
}

selector selector::descendants() const
{
    const std::vector<std::string> roots = paths_of(m_scope_roots);
    return with_predicate([roots](const config_node &node, const schema_query_context *)
                          { return matches_subtree(node.path(), roots, false); });
}

selector selector::at_depth(std::size_t depth) const
{
    const std::vector<std::string> roots = paths_of(m_scope_roots);
    return with_predicate([roots, depth](const config_node &node,
                                         const schema_query_context *)
                          { return matches_depth(node.path(), roots, depth); });
}

selector selector::under(std::string_view subpath) const
{
    std::vector<config_node>       roots = resolve_roots(m_scope_roots, subpath);
    const std::vector<std::string> paths = paths_of(roots);
    selector                       copy  = with_predicate([paths](const config_node &node,
                                                                  const schema_query_context *)
                                                          { return matches_subtree(node.path(), paths, true); });
    copy.m_scope_roots                   = std::move(roots);
    return copy;
}

}
