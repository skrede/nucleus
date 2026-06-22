# Keyref — Schema-Declared References by Identifier

A **keyref** is a field whose *value* names a target in a named identity namespace — the
read-side analog of XSD's `xs:keyref` (and the cousin of a SQL foreign key). nucleus
validates a keyref against dangling references and lets a host *dereference* it to the
target node, reusing the existing tree-addressing and query machinery — there is **no**
new `${...}` token.

Runnable example: [`examples/keyref.cpp`](../examples/keyref.cpp).

## Declaring a keyref

```cpp
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

// The namespace: the `name` of every output/endpoint, pooled into one identity group.
builder.register_identity_group(
    identity_group("endpoint_names", anchor::keyspace("endpoints"))
        .members({"output", "endpoint"}).field("name"));

// The keyref: route/target names a target in that namespace (declared AFTER the group).
builder.register_element(
    keyref_element("target", anchor::keyspace("route"), "endpoint_names"));
```

The target namespace is named **explicitly** via `into=` (here `"endpoint_names"`), never
inferred — which sidesteps the scope ambiguity XSD validators historically disagreed on.
The identity group must be registered **before** the keyref that references it; a keyref
into an unregistered namespace is a loud registration error.

## Validation — dangling references

At load time, a keyref whose value names no identifier in its namespace (within the slice)
is a loud `schema_violation`, with a `did you mean?` suggestion:

```
keyref 'route/target'='primery' matches no identifier in namespace 'endpoint_names' (did you mean 'primary'?)
```

A keyref is **orthogonal to `required`**: an *absent* keyref is not a dangling reference
(mark the field `required` separately to demand presence). Only a *present* value with no
match is dangling. The namespace scope is the resolved slice — with a primary key, each
selected strain has its own namespace.

## Dereference — `follow_keyref`

```cpp
#include "nucleus/query/query.h"

const auto ctx = space.query_context();
config_node keyref = cfg.root()["route"]["target"];

expected<config_node, error> target = follow_keyref(keyref, ctx);
if(target)
    std::cout << target->path();   // e.g. "endpoints/output[0]"
```

`follow_keyref` resolves to the **target node** — the member instance whose identifier
field equals the keyref's value — by walking the resolved tree via the `config_node` API
and the transient `schema_query_context` join (no stored cross-registry pointer). It
returns `expected<config_node, error>`:

- `errc::absent_key` — the node is not a keyref, has no value, or names no identifier.
- `errc::ambiguous_result` — the namespace somehow holds more than one match (a uniqueness
  violation the identity-group enforcer also reports).

It reuses the v0.3.0 tree-addressing core; it is **not** a `${...}` token (those resolve
values in-place during the fold; a keyref is a typed, dereferenceable identity reference).
