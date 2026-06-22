#ifndef HPP_GUARD_NUCLEUS_QUERY_QUERY_H
#define HPP_GUARD_NUCLEUS_QUERY_QUERY_H

#include "nucleus/query/schema_query_context.h"
#include "nucleus/query/node_role.h"
#include "nucleus/config_node.h"
#include "nucleus/expected.h"
#include "nucleus/error.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nucleus {

// Composed per-node predicate for the selector. Both arguments are stable
// for the lifetime of the visit; ctx may be nullptr for structural/kind queries.
using node_predicate = std::function<bool(const config_node &, const schema_query_context *)>;

// Composable, value-semantic predicate composer over the config_node walk.
// Built by query(); callers chain structural/kind/combinator selectors, then
// call a terminal to materialise results.
//
// Lifetime: borrows m_anchor (config) and m_ctx (config_space); neither must
// outlive the selector or any config_node results collected from it.
class selector
{
public:
    selector(config_node anchor, const schema_query_context *ctx)
        : m_anchor(std::move(anchor))
        , m_ctx(ctx)
        , m_predicate([](const config_node &, const schema_query_context *) { return true; })
    {}

    // Structural selectors — each narrows by adding a predicate via AND-chain.

    // Nodes whose direct parent is the anchor (exactly one level below).
    selector children() const;

    // All nodes reachable from the anchor, excluding the anchor itself.
    selector descendants() const;

    // Nodes exactly `depth` path segments below the anchor.
    selector at_depth(std::size_t depth) const;

    // Nodes whose path starts with the given absolute subpath prefix.
    selector under(std::string_view subpath) const;

    // Kind selectors — structural classification via config_node::kind().

    selector leaves() const;
    selector containers() const;
    selector repeated() const;

    // Combinator methods.

    // Union: a node is included when this selector OR the other would include it.
    selector or_(const selector &other) const;

    // Exclusion: a node is included when this selector would include it AND the
    // given predicate does NOT match it.
    selector excluding(node_predicate pred) const;

    // Terminal methods — trigger a single visit() traversal over the anchor.

    // Lazy iteration: calls fn for every matching node in pre-order DFS ordinal-stable order.
    void each(std::function<void(const config_node &)> fn) const;

    // Materialised: collects all matching nodes into a vector (ordinal-stable order).
    std::vector<config_node> collect() const;

    // Count of matching nodes.
    std::size_t count() const;

    // True when at least one node matches; short-circuits on the first match.
    bool exists() const;

    // Exactly-one semantics: errc::absent_key on zero matches,
    // errc::ambiguous_result on many matches (message includes the count).
    expected<config_node, error> one() const;

    // Typed collect: mirrors config::get_all_as<T>(); propagates the first converter
    // error encountered.
    template<typename T>
    expected<std::vector<T>, error> collect_as() const
    {
        std::vector<T> result;
        expected<void, error> first_error;
        each([&](const config_node &node) {
            if(!first_error)
                return;
            auto typed = node.template as<T>();
            if(!typed)
            {
                first_error = unexpected(typed.error());
                return;
            }
            result.push_back(std::move(*typed));
        });
        if(!first_error)
            return unexpected(first_error.error());
        return result;
    }

private:
    // Returns a copy of this selector with `p` AND-composed into the predicate.
    selector with_predicate(node_predicate p) const;

    config_node                m_anchor;
    const schema_query_context *m_ctx;
    node_predicate             m_predicate;
};

// Entry point: returns a selector anchored at `anchor` that borrows `ctx` transiently.
// The default predicate matches every node reachable via visit() from the anchor.
selector query(config_node anchor, const schema_query_context &ctx);

} // namespace nucleus

#endif
