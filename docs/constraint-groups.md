# Constraint Groups & Identity Groups

nucleus models a *relationship between sibling fields* as a **container-scoped
constraint group** — distinct from the per-element axes (`required`, `unique`,
`identity`). A group is declared against a container anchor and enforced on the
resolved, sliced tree; a resolved-tree violation is a loud diagnostic naming the
parties, surfaced through the `load_config` result as `errc::schema_violation`.

## Error channels

Duplicate-identifier and uniqueness failures use the vocabulary of the stage that detects them:

| Detection stage | Error code | When it fires |
|-----------------|------------|---------------|
| Source parsing | `errc::malformed_source` | One document is malformed in itself, such as two primary-keyed instances carrying the same key. |
| Layer composition | `errc::layering_violation` | Combining layers finds an identity collision before instances have final ordinals, so the diagnostic names layers rather than positions. |
| Resolved-tree validation | `errc::schema_violation` | Validation finds an identity or uniqueness collision after concrete paths exist, so the diagnostic names those paths. |

The error code alone is the discriminator a host branches on; the message supplies human detail in
the vocabulary available at that stage. When one validation run finds several violations, they
arrive as one `error` whose message begins with `schema validation failed:` and appends every reason
as `\n  - <reason>`. Reasons are therefore separated by exactly one newline. A host reads that
message rather than iterating a public violation structure.

There are two families over one substrate (a container-anchored member set + a clause +
a named diagnostic):

1. **Exclusion / choice** — cardinality over the active members of one container instance.
2. **Identity / key** — uniqueness of one identifier field across a set of element-types.

Runnable example: [`examples/schema/constraint_groups.cpp`](../examples/schema/constraint_groups.cpp).

## Exclusion groups

```cpp
#include "nucleus/schema/constraint_group.h"

builder.register_constraint_group(
    exclusion_group("cache_policy", anchor::keyspace("server/cache"))
        .members({"eager", "lru", "ttl"})
        .at_most(1));
```

The cardinality verb is the terminal — `at_most(n)`, `exactly(n)`, or `at_least(n)` —
counted over the **active** members of *each* instance of the anchor container (a
repeated container is checked per instance).

### Member activation — `when_value`

A bare member is active iff present. A `when_value` member is active iff its resolved
value equals the given value — the natural shape for a boolean/enum toggle. Mixed sets
are the norm:

```cpp
exclusion_group("cache_policy", anchor::keyspace("server/cache"))
    .member("eager", when_value("true"))   // active only when eager == "true"
    .member("lru")                          // active iff present
    .member("ttl")
    .at_most(1);
```

`when_value` matching is an **exact-string** compare &mdash; the resolved value must
equal the given string byte-for-byte. This is deliberately distinct from the
case-insensitive bool converter: `when_value("true")` activates on `true` but not on
`True` or `TRUE`. A member that resolves to an indexed instance path (a repeated
element, whose value lives at `member[0]` rather than the plain path) is matched at its
concrete instance.

`mutually_exclusive("name", anchor, {"a", "b"})` is sugar for a two-member `at_most(1)`.

### Choice over `all_of` bundles

A member may be an `all_of({...})` co-required bundle (all-or-none — a partially-present
bundle is its own violation). `choice(...)` selects exactly N bundles, for mode selection:

```cpp
builder.register_constraint_group(
    choice("auth_mode", anchor::keyspace("server/auth"))
        .option(all_of({"cert", "key"}))    // TLS bundle
        .option(all_of({"token"}))          // token bundle
        .exactly(1));
```

### Host-validator valve — `validate_group`

For the rare rule cardinality cannot express, a host validator runs over each resolved
container instance. There is deliberately **no** predicate/boolean-constraint grammar —
the valve is the escape hatch:

```cpp
builder.register_constraint_group(validate_group(
    "ttl_positive", anchor::keyspace("server/cache"),
    [](const config_node &cache) -> expected<void, std::string> {
        auto ttl = cache["ttl"].value();
        if(ttl.has_value() && *ttl == "0")
            return unexpected(std::string("ttl must be greater than zero"));
        return {};
    }));
```

## Identity groups

An identity group declares a **namespace** that pools one identifier `field` across the
instances of several repeated member element-types under one parent container. The
identifier must be **present and unique within a slice** (`xs:key` semantics):

```cpp
#include "nucleus/schema/identity_group.h"

builder.register_identity_group(
    identity_group("component_names", anchor::keyspace("server/pool"))
        .members({"worker", "gateway"})
        .field("name"));
```

- The collision key is the **identifier value alone** — the same value under two
  different element-types is also an error (a reference by name would be ambiguous).
  The diagnostic names both colliders and their element-types.
- Uniqueness scope is the resolved slice: with a primary key, each selected strain owns
  an independent namespace, so a name may recur across strains.
- The identifier is a **handle**, not a second primary key: it does not slice and is
  never consumed. It survives as readable data — the merge key for keyed composition and
  the target of a keyref.
- Namespace names are validated against a reserved-prefix carve-out so a host identifier
  can never shadow a builtin.

### Keyrefs beneath repetition

An identity namespace declared beneath a repeated container is local to each concrete enclosing
instance, so sibling instances may each declare the same identifier. A keyref into that namespace
does not carry an enclosing-instance qualifier, and dereferencing it through the read API is not
supported while the namespace container lies beneath repetition.

Declare a referenced namespace above the repetition instead. If several repeated instances
contribute identifiers to that lifted namespace, keep those identifiers globally distinct so a
keyref names exactly one target.

## What is out of scope

- A predicate / boolean-constraint grammar (the Helm/Terraform conditional-soup
  anti-feature). The host-validator valve covers the rare case.
- Per-member `field` override on an identity group (the field name is uniform across the
  member set; per-member override is a later extension).
