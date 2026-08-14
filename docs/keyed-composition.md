# Keyed Composition (cross-layer merge modes)

When a configuration is assembled from layered sources (a base document, an override, the
command line), a **repeated/identified collection** needs a rule for how a higher layer
combines with a lower one. nucleus declares that rule schema-side as a `merge_mode` on the
collection element. The mode changes only the *combine operation* within the existing
source-stack precedence order — it never changes layer ordering.

Runnable example: [`examples/schema/keyed_composition.cpp`](../examples/schema/keyed_composition.cpp).

## The three modes

```cpp
#include "nucleus/schema/schema.h"

// Default — wholesale_replace — needs no annotation:
builder.register_element(repeated_element("output", anchor::keyspace("endpoints")));

// unite / replace_by_key are set with merging(...):
builder.register_element(
    merging(repeated_element("output", anchor::keyspace("endpoints")), merge_mode::unite));
```

| Mode | Identifier only in lower | only in higher | same identifier in both layers |
|------|--------------------------|----------------|--------------------------------|
| `wholesale_replace` (default) | repeated container: kept unless the higher layer addresses its ordinal; repeated leaf: kept only when the higher layer omits the leaf | repeated container: kept; repeated leaf: the higher layer's whole list | repeated container: higher whole instance wins; repeated leaf: higher whole list wins |
| `unite` | kept | kept | **loud error** (duplicate across layers; strict-additive, no override) |
| `replace_by_key` | kept | kept | higher layer's **whole element** replaces the lower's |

- **`wholesale_replace`** is the default. A repeated container composes by ordinal position:
  each instance the higher layer supplies replaces, whole, the lower layer's instance at that
  ordinal, and an instance the higher layer does not address stays in place. For a repeated leaf,
  the unit is the whole value list under one enclosing instance: a higher layer that supplies the
  leaf replaces the lower list entirely, even when the higher list contains one value.
- **`unite`** unions the layers. It is strict-additive: re-introducing an existing identifier
  in a higher layer is a loud error (additions only, never an accidental override).
- **`replace_by_key`** unions the layers, but a matching identifier replaces that whole
  element with the higher layer's version — never a per-field merge.

## Primary-keyed containers without a merge mode

A container with a primary key and no explicit keyed merge mode follows selected-strain
composition. The chosen strain's transient key segment is consumed while its entries are re-laid
onto the declared resolved paths. Within a repeated container, a higher-rank entry displaces the
same field only in the concrete instance it addresses; other fields and sibling instances stay in
place. A repeated leaf remains a whole-list unit under one enclosing instance.

After the selected entries are re-laid, surviving instances are re-indexed to contiguous ordinals
in their existing relative order. Layering does not reorder them; only removal renumbers a later
instance. Every moved value retains its recorded provenance at the new ordinal. See the
[constraint-group error channels](constraint-groups.md#error-channels) for the public codes used by
duplicate-identifier and uniqueness failures.

## The merge key

`unite` and `replace_by_key` merge **by the identity-group field value**, never by ordinal
(the .NET `IConfiguration` array-merge-by-index bug is the canonical anti-pattern). They
therefore **require an `identity_group`** (see [constraint-groups.md](constraint-groups.md))
whose `field` is the merge key — a keyed mode with no covering identity group is a loud error,
as is an instance missing its key:

```cpp
builder.register_element(
    merging(repeated_element("output", anchor::keyspace("endpoints")), merge_mode::unite));
builder.register_identity_group(
    identity_group("output_names", anchor::keyspace("endpoints"))
        .members({"output"}).field("name"));   // "name" is the merge key
```

Surviving instances are re-indexed to contiguous ordinals in a stable order
(defining-layer rank, then original ordinal), and provenance travels with every instance.
The merge runs after all layers are folded and before strain slicing, so it sees every layer
and the key is still present; it composes correctly with primary-key strain selection (a
keyed collection nested inside a selected strain merges within that strain).

## `modify_by_key` is deferred — and why

A deep field-patch-by-key mode (`modify_by_key`) is **not** shipped. Every mature
layered-config system that added deep keyed-list merge regrets it: it needs authoritative
per-field schema metadata (which Kubernetes encodes in struct tags and Kustomize still
mis-merges without), it has ambiguous element ordering, it produces silent wrong-merge
surprises, and it needs a null/delete sentinel that then can't *set* null (RFC 7396).

The supported idiom for altering an inherited element is to **redeclare the whole element**
under `replace_by_key` and share values via references and tokenizers
(`${rel:}` / `${abs:}` — see [tree references](query-selector-api.md)), which is
deterministic, schema-free, and lossless.

## Interactions

- No shipped mode field-merges, so a single resolved element is always wholly from one layer
  — constraint groups (exclusion/choice) always see a single-layer element, unchanged.
- Identity-group uniqueness is enforced on the **merged** tree.
