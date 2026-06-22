# Query / Selector API

`#include "nucleus/query/query.h"`

The query API is a programmatic, fluent read surface over a resolved `config`.
It answers structured questions about the configuration tree — by structure,
kind, schema-role, owner-token, or strain — using a composable `selector`
evaluated lazily or materialised in a deterministic order.

---

## Overview

The query API joins two independent objects:

- a `config` (the resolved, immutable, schema-free tree), and
- a `schema_query_context` (a transient facade carrying schema authority into
  the query without storing any cross-registry pointer).

Neither object is modified by a query. One space and one config can serve any
number of queries.

---

## Entry Point

### `config_space::query_context()`

```cpp
schema_query_context ctx = space.query_context();
```

Builds a transient `schema_query_context` from the sealed `config_space`.
The context pre-builds two indices at construction time:

- a role index (canonical declared path → `node_role`)
- an owner index (canonical declared path → `owner_token`)

**Lifetime:** `ctx` borrows `space`; it must not outlive the `config_space`.
Keep both alive for the duration of any query that uses the context.

### `query(anchor, ctx)`

```cpp
selector query(config_node anchor, const schema_query_context &ctx);
```

Returns a `selector` anchored at `anchor` that borrows `ctx` transiently.
The default selector matches every node reachable from the anchor.

```cpp
const auto ctx = space.query_context();
auto nodes = query(cfg.root(), ctx).leaves().collect();
```

---

## Structural Selectors

Structural selectors narrow by position relative to the anchor.

| Method | Meaning |
|--------|---------|
| `children()` | Nodes exactly one level below the anchor (direct children). |
| `descendants()` | All transitive nodes reachable from the anchor, excluding the anchor itself. |
| `at_depth(n)` | Nodes exactly `n` path segments below the anchor. |
| `under(subpath)` | Nodes whose path starts with the given absolute subpath. |

Selectors compose by AND-chaining:

```cpp
// Leaf children of cluster:
query(cfg.root()["cluster"], ctx).children().leaves().collect();
```

---

## Kind Selectors

Kind selectors classify nodes structurally via `config_node::kind()`.

| Method | Matches |
|--------|---------|
| `leaves()` | Scalar nodes (`node_kind::scalar`). |
| `containers()` | Container nodes (`node_kind::container`). |
| `repeated()` | Repeated-container structural nodes (`node_kind::repeated`). |

**Note:** `leaves()` includes the retained primary-key leaf (Phase 22 D-10).
For schema-authoritative role classification, use `role()` instead (see below).

---

## Schema-Role Selectors

Schema-role selectors use the `schema_query_context` role index, which is
built from the declared schema, not from the live config tree. This makes them
correct at the zero-instance boundary.

```cpp
selector role(node_role r);
```

| `node_role` | Matches |
|-------------|---------|
| `node_role::primary_key` | The declared identity (pkey) leaf. |
| `node_role::leaf` | Declared leaf elements (non-identity). |
| `node_role::container` | Declared container elements (non-repeated). |
| `node_role::repeated_container` | Declared repeated containers with children. |

**Zero-instance boundary:** A declared-repeated container with no live instances
classifies as `repeated_container` via the schema index even when no config keys
exist for it. This is schema authority: the role comes from the declaration, not
from the tree.

```cpp
// Check schema authority directly (zero-instance case):
CHECK(ctx.is_repeated_container("cluster/node"));

// The live query returns empty (no instances), but the schema classification holds.
auto nodes = query(cfg.root(), ctx).role(node_role::repeated_container).collect();
```

---

## Owner-Token Selection

```cpp
selector owned_by(owner_token token);
```

Returns nodes registered under the supplied `owner_token` by `==`-match only.
The owner index is a transient `path → owner_token` map derived from the
registration ledger; there is no owner field on `schema_element`.

**Semantics:**

- A tagged token (`owner_token(std::string("name"))`) matches nodes registered
  with a token whose payload compares equal — same type, same value.
- Two anonymous (default-constructed) `owner_token` objects are NEVER equal.
  Each carries a distinct pointer identity. An anonymous token matches nothing
  unless the exact same token object that was used at registration is supplied.
- A never-registered token yields an **empty** result, not an error.

```cpp
owner_token net(std::string("network.module"));
// Register elements with net, then:
auto net_nodes = query(cfg.root(), ctx).owned_by(net).collect();
auto anon      = owner_token{}; // matches nothing (new identity)
```

---

## Strain Selection

```cpp
selector in_strain();
```

Selects all nodes that belong to the same ordinal instance as the anchor.
The instance is determined from the anchor's position relative to the
primary-key container.

**Rules:**

- The anchor must be inside a specific ordinal instance (e.g. `server[0]`).
  A container-level anchor (not within a `[N]` instance) yields an **empty**
  result, not an error.
- Results are prefix-filtered to the exact `[N]` instance — `server[0]` and
  `server[1]` never overlap.
- The retained primary-key leaf is included (D-10).

```cpp
const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
auto strain = query(anchor0, ctx).in_strain().collect();
// All paths start with "cluster/server[0]/"; server[1] fields are excluded.
```

---

## Terminals

Terminals trigger a single traversal of the anchor and return a result.

| Terminal | Returns | Behaviour |
|----------|---------|-----------|
| `each(fn)` | `void` | Lazy: calls `fn` once per matching node. |
| `collect()` | `vector<config_node>` | Materialised; ordinal-stable pre-order DFS. |
| `count()` | `size_t` | Number of matching nodes. |
| `exists()` | `bool` | True if at least one node matches; short-circuits. |
| `one()` | `expected<config_node, error>` | **Loud** exactly-one semantics (see below). |
| `collect_as<T>()` | `expected<vector<T>, error>` | Typed; propagates first converter error. |

### Ordering guarantee

`collect()` and `each()` emit nodes in pre-order DFS, ordinal-stable order:
repeated instances appear in numeric ordinal order (`[0]`, `[1]`, ..., `[10]`),
never in `std::map` lexicographic order (`[0]`, `[1]`, `[10]`, `[2]`).
This mirrors the ordering of `config::get_all()`.

### `one()` — Loud Exactly-One Semantics

`one()` is loud: it errors on both zero and many matches.

```cpp
expected<config_node, error> one() const;
```

| Condition | Error code | Message |
|-----------|-----------|---------|
| Zero matches | `errc::absent_key` | Includes "zero" |
| Many matches | `errc::ambiguous_result` | Includes the match count and "nodes" |
| Exactly one | Success | — |

`one()` is designed for identity-style queries ("select THE primary key") where
any deviation from exactly one match is a program logic error that should be
surfaced immediately.

```cpp
// Two server instances -> ambiguous_result:
auto result = query(cfg.root(), ctx).role(node_role::primary_key).one();
if(!result)
    std::cerr << result.error().message << '\n'; // "2 nodes matched ..."

// Single-instance subtree -> success:
auto pkey = query(cfg.root()["cluster"]["server"][std::size_t{0}], ctx)
                .role(node_role::primary_key)
                .one();
```

---

## Combinators

Combinators compose selectors without re-scanning the tree.

| Method | Semantics |
|--------|-----------|
| AND (chaining) | Each additional filter narrows the result set. |
| `or_(other)` | Union: a node is included if either selector matches it. |
| `excluding(pred)` | NOT: removes nodes matching the given `node_predicate`. |

```cpp
// OR: leaves OR containers under cluster/server:
auto left  = query(cfg.root(), ctx).under("cluster/server").leaves();
auto right = query(cfg.root(), ctx).under("cluster/server").containers();
auto both  = left.or_(right).collect();

// NOT: descendants of cluster that are not under cluster/server:
node_predicate is_server = [](const config_node &n, const schema_query_context *) {
    return n.path().find("cluster/server") != std::string_view::npos;
};
auto non_server = query(cfg.root()["cluster"], ctx)
                      .descendants()
                      .excluding(std::move(is_server))
                      .collect();
```

### Single-pass evaluation

The selector evaluates the composed predicate chain in a **single pass** over
the anchor's subtree via `config_node::visit()`. Predicates are called once per
node (O(N)); there is no O(N²) per-node re-scan of the keyspace. Schema-role
lookups are O(log N) via the pre-built index.

---

## Lifetime Contract

**`config_node` results** borrow the `config` they were derived from. A result
`config_node` must not outlive its source `config`. Storing results and then
destroying the `config` is undefined behaviour (caught by AddressSanitizer).

**`schema_query_context`** borrows the `config_space`. The context must not
outlive the space it was built from.

**Recommended pattern:**

```cpp
config_space space = ...;          // sealed, long-lived
config cfg = load_config(...);     // resolved, same lifetime as space

{
    schema_query_context ctx = space.query_context(); // transient
    auto nodes = query(cfg.root(), ctx).leaves().collect();
    // Use nodes here — all three (space, cfg, ctx) are live.
}
// ctx destroyed; cfg and space are still valid.
```

A runnable example covering these patterns lives in
[`examples/query.cpp`](../examples/query.cpp).
