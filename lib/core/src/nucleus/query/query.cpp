#include "nucleus/query/query.h"
#include "nucleus/config.h"
#include "nucleus/error.h"

#include <string>

namespace nucleus {

// Returns a copy with `p` ANDed into the existing predicate.
selector selector::with_predicate(node_predicate p) const
{
    selector copy = *this;
    node_predicate old = m_predicate;
    copy.m_predicate = [old = std::move(old), p = std::move(p)](
                           const config_node &node, const schema_query_context *ctx) {
        return old(node, ctx) && p(node, ctx);
    };
    return copy;
}

selector selector::children() const
{
    const std::string anchor_path{m_anchor.path()};
    return with_predicate([anchor_path](const config_node &node, const schema_query_context *) {
        std::string_view np = node.path();
        if(np == anchor_path)
            return false;
        // The node is a direct child when its path is exactly anchor + one segment.
        // For a plain container anchor "a/b", children are "a/b/x" or "a/b/x[n]".
        // For a repeated anchor "a/b", direct ordinal instances are "a/b[n]".
        std::string_view remainder;
        if(anchor_path.empty())
        {
            // Root anchor: direct children have no '/' in their path.
            remainder = np;
        }
        else if(np.starts_with(anchor_path))
        {
            // Could be "a/b/x" (child via '/') or "a/b[n]" (ordinal instance).
            std::string_view after = np.substr(anchor_path.size());
            if(after.empty())
                return false;
            if(after[0] == '/')
                remainder = after.substr(1);
            else if(after[0] == '[')
                remainder = after; // "a/b[n]" — the '[n]' is the full remainder
            else
                return false; // partial name match, not a child
        }
        else
        {
            return false;
        }
        // A direct child has no further '/' or '[' after the first segment boundary.
        // remainder is either "name", "name[n]", or "[n]".
        auto slash_pos = remainder.find('/');
        return slash_pos == std::string_view::npos;
    });
}

selector selector::descendants() const
{
    const std::string anchor_path{m_anchor.path()};
    return with_predicate([anchor_path](const config_node &node, const schema_query_context *) {
        return node.path() != anchor_path;
    });
}

selector selector::at_depth(std::size_t depth) const
{
    const std::string anchor_path{m_anchor.path()};
    return with_predicate([anchor_path, depth](const config_node &node,
                                               const schema_query_context *) {
        std::string_view np = node.path();
        if(np == anchor_path)
            return false;

        // Count '/' separators in the suffix after the anchor prefix.
        // For a root anchor (empty path) the suffix is the full path.
        std::string_view suffix;
        if(anchor_path.empty())
        {
            suffix = np;
        }
        else if(np.starts_with(anchor_path))
        {
            std::string_view after = np.substr(anchor_path.size());
            if(after.empty())
                return false;
            // Repeated instance: "cluster/node[0]" after "cluster/node" → "[0]"
            // Container child:   "cluster/port"   after "cluster"       → "/port"
            if(after[0] != '/' && after[0] != '[')
                return false; // partial name match
            suffix = after;
        }
        else
        {
            return false;
        }

        // The number of '/' separators in the suffix equals the relative depth.
        std::size_t seps = static_cast<std::size_t>(
            std::count(suffix.begin(), suffix.end(), '/'));
        return seps == depth;
    });
}

selector selector::under(std::string_view subpath) const
{
    const std::string prefix{subpath};
    return with_predicate([prefix](const config_node &node, const schema_query_context *) {
        std::string_view p = node.path();
        if(!p.starts_with(prefix))
            return false;
        // Ensure the match is a full segment boundary, not a partial name.
        if(p.size() > prefix.size())
            return p[prefix.size()] == '/';
        return p.size() == prefix.size();
    });
}

selector selector::leaves() const
{
    return with_predicate([](const config_node &node, const schema_query_context *) {
        return node.kind() == node_kind::scalar;
    });
}

selector selector::containers() const
{
    return with_predicate([](const config_node &node, const schema_query_context *) {
        return node.kind() == node_kind::container;
    });
}

selector selector::repeated() const
{
    return with_predicate([](const config_node &node, const schema_query_context *) {
        return node.kind() == node_kind::repeated;
    });
}

selector selector::or_(const selector &other) const
{
    node_predicate left  = m_predicate;
    node_predicate right = other.m_predicate;
    const schema_query_context *other_ctx = other.m_ctx;

    selector copy = *this;
    copy.m_predicate = [left = std::move(left), right = std::move(right), other_ctx](
                           const config_node &node, const schema_query_context *ctx) {
        return left(node, ctx) || right(node, other_ctx);
    };
    return copy;
}

selector selector::excluding(node_predicate pred) const
{
    return with_predicate([pred = std::move(pred)](
                              const config_node &node, const schema_query_context *ctx) {
        return !pred(node, ctx);
    });
}

// Single-pass evaluation: one visit() call per each() invocation.
// The visit lambda always returns true so predicate failures at a parent
// never suppress matching descendants. Absent nodes are never emitted:
// a navigation to a non-existent path visits that node but yields no results.
void selector::each(std::function<void(const config_node &)> fn) const
{
    const node_predicate &pred = m_predicate;
    const schema_query_context *ctx = m_ctx;
    m_anchor.visit([&pred, ctx, &fn](const config_node &node) -> bool {
        if(node.exists() && pred(node, ctx))
            fn(node);
        return true;
    });
}

std::vector<config_node> selector::collect() const
{
    std::vector<config_node> result;
    each([&](const config_node &n) { result.push_back(n); });
    return result;
}

std::size_t selector::count() const
{
    std::size_t n = 0;
    each([&](const config_node &) { ++n; });
    return n;
}

// Short-circuits on the first match to avoid unnecessary traversal.
bool selector::exists() const
{
    bool found = false;
    const node_predicate &pred = m_predicate;
    const schema_query_context *ctx = m_ctx;
    m_anchor.visit([&pred, ctx, &found](const config_node &node) -> bool {
        if(node.exists() && pred(node, ctx))
        {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

expected<config_node, error> selector::one() const
{
    auto results = collect();
    if(results.empty())
        return unexpected(error{errc::absent_key, "query matched zero nodes; exactly one expected"});
    if(results.size() > 1)
        return unexpected(error{errc::ambiguous_result,
                                "query matched " + std::to_string(results.size()) +
                                    " nodes; one() requires exactly one match"});
    return std::move(results.front());
}

selector query(config_node anchor, const schema_query_context &ctx)
{
    return selector{std::move(anchor), &ctx};
}

} // namespace nucleus
