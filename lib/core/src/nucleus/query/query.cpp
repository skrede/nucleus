#include "nucleus/query/query.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// Returns a copy with `p` ANDed into the existing predicate.
selector selector::with_predicate(node_predicate p) const
{
    selector copy = *this;
    node_predicate old = m_predicate;
    copy.m_predicate = [base = std::move(old), added = std::move(p)](
                           const config_node &node, const schema_query_context *ctx) {
        return base(node, ctx) && added(node, ctx);
    };
    return copy;
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

// schema index is authoritative; 0-instance repeated yields no matching config_nodes
// (none exist to visit), but the schema context still classifies the declared path.
selector selector::role(node_role r) const
{
    return with_predicate([r](const config_node &node, const schema_query_context *ctx) -> bool {
        if(!ctx)
            return false;
        const std::string canonical = ctx->canonicalize(node.path());
        return ctx->role_of(canonical) == r;
    });
}

selector selector::owned_by(owner_token token) const
{
    return with_predicate(
        [held_token = std::move(token)](const config_node &node,
                                        const schema_query_context *ctx) -> bool {
            if(!ctx)
                return false;
            const std::string canonical = ctx->canonicalize(node.path());
            const auto owner = ctx->owner_of(canonical);
            return owner.has_value() && *owner == held_token;
        });
}

selector selector::in_strain() const
{
    // Compute strain prefix once at call time, not per node visit.
    const std::string pkey_cont{m_ctx.primary_key_container()};
    const std::string_view anchor_path = m_anchor.path();

    // Anchor must be within a specific ordinal instance of pkey_cont.
    const bool within_instance = anchor_path.size() > pkey_cont.size()
        && anchor_path.starts_with(pkey_cont)
        && anchor_path[pkey_cont.size()] == '[';

    if(!within_instance)
        return with_predicate([](const config_node &, const schema_query_context *) { return false; });

    const std::string_view after_cont = anchor_path.substr(pkey_cont.size());
    const auto close_bracket = after_cont.find(']');
    if(close_bracket == std::string_view::npos)
        return with_predicate([](const config_node &, const schema_query_context *) { return false; });

    // e.g. pkey_cont="cluster/server" + "[0]" → "cluster/server[0]"
    const std::string strain_prefix = pkey_cont + std::string(after_cont.substr(0, close_bracket + 1));

    return with_predicate(
        [strain_prefix](const config_node &node, const schema_query_context *) -> bool {
            const std::string_view p = node.path();
            if(p == strain_prefix)
                return true;
            // Child or descendant: path continues with '/' after the strain prefix.
            return p.size() > strain_prefix.size()
                && p.starts_with(strain_prefix)
                && p[strain_prefix.size()] == '/';
        });
}

selector selector::or_(const selector &other) const
{
    node_predicate left  = m_predicate;
    node_predicate right = other.m_predicate;

    // Capture a COPY of the other selector's context: it may be a temporary, so
    // holding its address would dangle once the full expression ends.
    selector copy = *this;
    copy.m_predicate = [lhs = std::move(left), rhs = std::move(right),
                        other_ctx = other.m_ctx](
                           const config_node &node, const schema_query_context *ctx) {
        return lhs(node, ctx) || rhs(node, &other_ctx);
    };
    return copy;
}

selector selector::excluding(node_predicate pred) const
{
    return with_predicate([inner = std::move(pred)](
                              const config_node &node, const schema_query_context *ctx) {
        return !inner(node, ctx);
    });
}

// Single-pass evaluation: one visit() call per each() invocation.
// The visit lambda always returns true so predicate failures at a parent
// never suppress matching descendants. Absent nodes are never emitted:
// a navigation to a non-existent path visits that node but yields no results.
void selector::each(std::function<void(const config_node &)> fn) const
{
    const node_predicate &pred = m_predicate;
    const schema_query_context *ctx = &m_ctx;
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
    const schema_query_context *ctx = &m_ctx;
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
    return selector{std::move(anchor), ctx};
}

}
