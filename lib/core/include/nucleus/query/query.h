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

// Composed per-node predicate for the selector. Both arguments are stable for
// the lifetime of the visit; the selector always passes the address of its owned
// context snapshot (structural/kind predicates simply ignore it).
using node_predicate = std::function<bool(const config_node &, const schema_query_context *)>;

// Composable, value-semantic predicate composer over the config_node walk.
// Built by query(); callers chain structural/kind/combinator selectors, then
// call a terminal to materialise results.
//
// Lifetime: owns m_ctx by value (a self-contained schema snapshot); borrows
// m_anchor (config), which must outlive the selector and its collected results.
class selector
{
public:
    selector(config_node anchor, schema_query_context ctx)
        : m_anchor(std::move(anchor))
        , m_ctx(std::move(ctx))
        , m_predicate([](const config_node &, const schema_query_context *) { return true; })
    {}

    // Structural selectors — each narrows by adding a predicate via AND-chain.

    // Nodes whose direct parent is the anchor (exactly one level below).
    selector children() const;

    // All nodes reachable from the anchor, excluding the anchor itself.
    selector descendants() const;

    // Nodes exactly `depth` `/`-separator segments below the anchor. Diverges
    // from children() on a repeated anchor: a `[n]` instance boundary is not a
    // separator, so at_depth(1) returns the instances' fields, whereas
    // children() treats each `[n]` instance as one level below the anchor.
    selector at_depth(std::size_t depth) const;

    // Nodes whose path starts with the given absolute subpath prefix.
    selector under(std::string_view subpath) const;

    // Kind selectors — structural classification via config_node::kind().

    selector leaves() const;
    selector containers() const;
    selector repeated() const;

    // Schema-aware selectors — require ctx (return empty if ctx is nullptr).

    // Nodes whose schema-authoritative role matches r. Uses the ctx role index
    // built from declared schema elements, so zero-instance repeated containers
    // are still returned when r == node_role::repeated_container.
    selector role(node_role r) const;

    // Nodes registered under the supplied owner_token via ==-match only.
    // A never-registered token yields an empty result; an anonymous
    // (default-constructed) token matches nothing unless the EXACT same
    // token object was registered.
    selector owned_by(owner_token token) const;

    // Nodes belonging to the same ordinal instance as the anchor. The instance
    // is derived from the anchor's position under the primary-key container.
    // A container-level anchor (not within a specific instance) yields empty,
    // not an error.
    selector in_strain() const;

    // Combinator methods.

    // Union of predicates, evaluated over THIS selector's anchor-subtree traversal:
    // a node reachable only from the other selector's anchor is not visited.
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

    config_node          m_anchor;
    schema_query_context m_ctx;
    node_predicate       m_predicate;
};

// Entry point: returns a selector anchored at `anchor` that copies `ctx` in.
// The default predicate matches every node reachable via visit() from the anchor.
selector query(config_node anchor, const schema_query_context &ctx);

// Dereferences a keyref leaf to its target node: the member instance in the keyref's
// named identity namespace whose identifier field equals the keyref's value. Reuses
// the v0.3.0 tree-addressing walk and the transient schema_query_context join -- NOT a
// ${...} token. Returns errc::absent_key when the node is not a keyref, has no value, or
// names no identifier (a dangling reference); errc::ambiguous_result when the namespace
// somehow holds more than one match (a uniqueness violation the enforcer also flags).
expected<config_node, error> follow_keyref(const config_node &keyref_leaf,
                                           const schema_query_context &ctx);

} // namespace nucleus

#endif
