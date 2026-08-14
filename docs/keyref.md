# Keyref — Schema-Declared References by Identifier

A **keyref** is a field whose value names a target in a named identity namespace,
the read-side analog of XSD's `xs:keyref` and the cousin of a SQL foreign key.
nucleus validates each present reference during load and lets a host dereference
it to the target node. This uses the resolved configuration tree; it introduces
no new `${...}` token.

Runnable example: [`examples/references/keyref.cpp`](../examples/references/keyref.cpp).

## Declaring a keyref

```cpp
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

// The namespace pools the `name` field of every output and endpoint instance.
builder.register_identity_group(
    identity_group("endpoint_names", anchor::keyspace("endpoints"))
        .members({"output", "endpoint"}).field("name"));

// route/target names one member of that namespace.
builder.register_element(
    keyref_element("target", anchor::keyspace("route"), "endpoint_names"));
```

The target namespace is named explicitly, never inferred. The identity group
must be registered before a keyref that names it; an unregistered namespace is
a loud registration error.

## Repeated-scope candidate derivation

Validation and `follow_keyref()` use the same candidate derivation:

1. Starting from the concrete keyref path, bind the deepest ordinal-bearing
   prefix whose canonical path is shared with the target namespace container.
2. Search every concrete instance of each target repeated dimension that is not
   bound by that prefix.
3. Compare identifier values without collapsing duplicate candidates.
4. Accept or return a target only when exactly one candidate matches.

Consider a namespace `output_names` declared under `cluster/node`, where both
`node` and its `output` member are repeated:

```text
cluster/node[0]/output[0]/name
cluster/node[0]/output[0]/local_target
cluster/node[0]/output[1]/name
cluster/node[1]/output[0]/name
cluster/global_target
```

For `cluster/node[0]/output[0]/local_target`, the maximal shared repeated scope
is `cluster/node[0]`. The `output` ordinal is not part of the namespace
container, so it remains unbound and every `output[m]` inside `node[0]` is
searched. The reference can therefore resolve from `output[0]` to
`output[1]` when that is the unique value match.

A reference at `cluster/global_target` shares no concrete repeated scope with
the namespace. Its qualified scope is reported as `<unbound>`, and every
`output[m]` beneath every `node[n]` is searched. Collection order never chooses
a winner: zero matches are dangling and multiple matches are ambiguous.

This yields three useful cases:

- **Bound:** sibling `node[0]` and `node[1]` scopes may each reuse the same
  identifier; a local reference resolves only within its own concrete node.
- **Partially bound:** a local reference binds `node[n]` but searches every
  unbound `output[m]` within that node.
- **Unbound:** a reference outside `node` searches all nodes and outputs, and
  succeeds only when its value is globally unique across those candidates.

The executable contract covers candidate counts 0, 1, 2, and 10 for partially
bound and globally unbound references. Exactly one resolves; zero and every
many-candidate case fail rather than taking the first target.

## Load-time validation

Every present keyref must have exactly one candidate. A dangling value and an
ambiguous value both fail the load with `errc::schema_violation`. Therefore,
every present keyref in a successfully loaded configuration is dereferenceable
under the same scope rule.

Presence is an independent axis. An optional keyref may be absent without being
dangling; set its ordinary `required` field to demand presence. Once a value is
present, optionality does not excuse zero or multiple targets.

Load diagnostics are bounded. They name the concrete reference path, reference
value, namespace, qualified scope, and match count. They do not enumerate
candidate paths, so a diagnostic remains bounded even when the namespace is
large. A zero-match diagnostic may also include a nearest-identifier suggestion:

```text
keyref 'route/target'='primery' matches no identifier (0 targets) in namespace 'endpoint_names' within qualified scope '<unbound>' (did you mean 'primary'?)
```

## Dereference — `follow_keyref`

```cpp
#include "nucleus/query/query.h"

const auto ctx = space.query_context();
config_node keyref = cfg.root()["route"]["target"];

expected<config_node, error> target = follow_keyref(keyref, ctx);
if(target)
    std::cout << target->path();   // e.g. "endpoints/output[0]"
```

`follow_keyref()` returns the target member instance, not its identifier leaf.
It uses the same scope-qualified candidate index and exactly-one rule as load
validation. Defensive calls against a manually constructed or otherwise
unvalidated configuration map cardinality to the public read errors:

- exactly one candidate returns that target node;
- zero candidates returns `errc::absent_key`;
- multiple candidates return `errc::ambiguous_result`.

A node that is not a declared keyref or has no value also returns
`errc::absent_key`. Zero- and many-target errors use the same bounded fields as
load diagnostics and do not enumerate candidate paths.

Keyrefs are distinct from `${...}` tokens. Tokens rewrite values during the
load pipeline; a keyref remains a schema-declared, dereferenceable identity
reference in the resolved configuration.
