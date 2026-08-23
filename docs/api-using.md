# Types you use

The host-side vocabulary: the types a program instantiates, passes in, and reads
back. None of these requires subclassing. For the seams a host extends, see
[Seams you extend](api-extending.md).

## Contents

- [The lifecycle at a glance](#lifecycle)
- [`config_space_builder` — registration](#builder)
- [Declaring a schema: `schema_element`, `anchor`, free factories](#schema)
- [Typed fields: `typed_element<T>`, `register_converter`](#typed)
- [Keying model: primary key, uniqueness, strains](#keying)
- [Tree references and pkey shortcuts](#tree-references)
- [`config_space` — the sealed space](#space)
- [Composing sources: `source_stack`](#stack)
- [`load_config()` and `load_options`](#load)
- [Capability pre-flight: `check_capabilities`](#preflight)
- [`config` — the resolved result](#configuration)
- [`config_node` — the walk API](#config_node)
- [Provenance: `origin`](#provenance)
- [`key_path` — addressing the keyspace](#key_path)
- [Emitting: templates and documents](#emit)
- [`expected<T, E>` and `error` — fallible returns](#expected)
- [`owner_token` — opaque identity](#owner_token)
- [Diagnostics: `suggest_keys`, `conflict_report`](#diagnostics)
- [Completion: `generate_completion`, `shell`](#completion)

---

<a id="lifecycle"></a>
## The lifecycle at a glance

```
config_space_builder  --register_* / install_tokenizer-->  build()
        |
        v
config_space (sealed, immutable, reusable)
        |
        v
nucleus::load_config(space, source_stack, load_options)  -->  expected<config, error>
```

A builder accepts registrations and `build()` seals it into an immutable
`config_space`. The free function `nucleus::load_config` folds an explicitly
composed `source_stack` against the sealed space and yields an immutable
`config`. The space is never modified by a load, so one space serves many
loads — even concurrently (see [`examples/composition/reusable_space.cpp`](../examples/composition/reusable_space.cpp)).
The stack is borrowed by the load, not consumed, so one stack serves many loads
too. Every fallible step reports a typed [`error`](#expected): an `errc` code a
program branches on plus a verbatim human-readable message.

---

<a id="builder"></a>
## `config_space_builder` — registration

`#include "nucleus/config_space.h"`

The mutable front end. It owns the schema, tokenizer, and converter registries
plus the host registration policy and the claim/conflict ledger. Move-only.

```cpp
config_space_builder builder;

registration_result register_element(schema_element element, owner_token owner = {});
registration_result register_schema(std::string key_path, owner_token owner = {});
registration_result register_tokenizer(std::string name, owner_token owner = {});
registration_result install_tokenizer(tokenizer tok, owner_token owner = {});
registration_result register_converter(std::type_index id,
    std::function<expected<std::any, std::string>(std::string_view)> conv,
    owner_token owner = {});
template<typename T>
registration_result register_converter(/* conv */, owner_token owner = {});  // keyed by typeid(T)
registration_result set_registration_policy(std::shared_ptr<registration_policy> policy);

config_space_builder &name(std::string space_name);  // the identity each source validates at its boundary; empty = unnamed

std::size_t schema_count() const noexcept;
std::size_t tokenizer_count() const noexcept;
std::size_t converter_count() const noexcept;
std::vector<conflict_report> conflicts() const;

config_space build();   // seals; the builder is spent afterwards
```

- `registration_result` is `expected<void, error>`: truthy on success. On
  failure the code is `errc::rejected_registration` (the registration policy's
  reason, verbatim, in `message`) or `errc::sealed_builder`.
- Check each registration result: `if(!builder.register_element(...)) ...`.
- Every registration carries an opaque `owner_token` and is first offered to the
  [registration policy](api-extending.md#registration_policy).
- After `build()`, every `register_*` / `install_*` call is a loud state-machine
  error (`errc::sealed_builder`), never a silent no-op.

See [`examples/basics/quickstart.cpp`](../examples/basics/quickstart.cpp).

---

<a id="schema"></a>
## Declaring a schema

`#include "nucleus/schema/schema.h"` and `"nucleus/schema/anchor.h"`

The schema is the authority over what keys may exist. A `schema_element` is one
declared node; free factory functions build the kinds fluently:

```cpp
schema_element element(std::string name, anchor at);
schema_element required_element(std::string name, anchor at);
schema_element identity_element(std::string name, anchor at);
schema_element primary_key_element(std::string name, anchor at);   // alias for identity_element
schema_element unique_element(std::string name, anchor at);
schema_element repeated_element(std::string name, anchor at);
schema_element enum_element(std::string name, anchor at, std::vector<std::string> values);

// In "nucleus/schema/converters.h":
template<typename T> schema_element typed_element(std::string name, anchor at);  // built-in scalar converter
template<typename T> schema_element typed_element(std::string name, anchor at,
    std::function<expected<std::any, std::string>(std::string_view)> conv);
template<typename T> schema_element repeated_typed_element(std::string name, anchor at /*, conv */);
template<typename T> schema_element registered_element(std::string name, anchor at);  // converter from the registry
```

The underlying struct exposes the independent axes the factories set:

```cpp
struct schema_element {
    std::string name;                        // the leaf segment
    anchor at = anchor::root();              // where it attaches
    bool required = false;                   // must carry a value at load
    bool identity = false;                   // the parent container's primary key / slice selector
    bool unique = false;                     // value distinct across sibling instances
    bool repeated = false;                   // N instances each occupy a distinct ordinal slot
    std::vector<std::string> allowed_values; // closed set; empty = unconstrained
    // plus the typed seam: converter, type_identity (set by typed_element<T>)
    key_path declared_path() const;          // anchor path + name
    key_path container() const;              // the parent path
    bool enforces_uniqueness() const noexcept;  // identity || unique
};
```

### `anchor` — where an element attaches

`anchor::root()` introduces a top-level node; `anchor::keyspace(path)` attaches
under an already-declared node. Referential integrity is enforced at attach
time. At load, the schema rejects undeclared keys (with a nearest-key
suggestion), missing `required` fields, and values outside an `enum_element`'s
`allowed_values`. A space whose schema declares no elements applies no content
gate — an empty schema is not a claim that nothing is allowed.

```cpp
if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
    return 1;
if(!builder.register_element(
    nucleus::required_element("host", nucleus::anchor::keyspace("server"))))
    return 1;
if(!builder.register_element(
    nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                          {"http", "https"})))
    return 1;
```

See [`examples/schema/schema.cpp`](../examples/schema/schema.cpp).

### Repeated elements

`repeated` is legal on **any** element — leaf or container. N sibling instances
each occupy a distinct zero-based ordinal slot in the resolved keyspace:

- **Repeated leaf** — instances are scalars: `config/tags[0]`, `config/tags[1]`.
- **Repeated container** — instances are indexed subtrees: `cluster/node[0]/port`,
  `cluster/node[1]/port`.
- **Nested repetition** — composes: `cluster/node[0]/route[1]/target`.

Ordinals are zero-based and assigned in document order. Replacement across
layers is per concrete instance: a higher-precedence source layer replaces each
instance it addresses — wholly, never field by field — and leaves instances it
does not address in place. A two-instance layer over a three-instance base
therefore resolves to three instances. Under nested repetition the unit is the
innermost instance the addressed path names: a layer setting
`cluster/node[1]/route[0]/target` replaces `route[0]` wholly and leaves both
`route[1]` and the rest of `node[1]` in place, whereas a layer setting
`cluster/node[1]/port` replaces `node[1]` wholly, nested routes included.
`extend=` may not target a repeated container (`errc::layering_violation`).
`repeated` requires no `identity` or `unique` declaration: the three axes are
orthogonal — `repeated` governs placement, `identity` governs strain selection,
and `unique` governs value validation. An element cannot be both `repeated` and
`unique`.

```cpp
// Repeated container: each <node> becomes an indexed subtree.
if(!builder.register_element(
    nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))))
    return 1;
if(!builder.register_element(
    nucleus::element("port", nucleus::anchor::keyspace("cluster/node"))))
    return 1;
```

Feeding a repeated element requires a source layer with the `duplicate_keys`
capability (XML, argv, or the runtime source).

**Indexed FQN addressing.** The resolved keyspace stores indexed scalars
(`cluster/node[0]/port`, `cluster/node[1]/port`). Access rules:

| Call | Behavior |
|------|----------|
| `cfg.get("cluster/node[0]/port")` | returns the scalar at that exact ordinal |
| `cfg.get("cluster/node/port")` | returns `nullopt` (unindexed crossing) |
| `cfg.get_as<T>("cluster/node[0]/port")` | returns `expected<T, error>` at that ordinal |
| `cfg.get_as<T>("cluster/node/port")` | returns `errc::index_required` naming the container and instance count |
| `cfg.get_all("cluster/node/port")` | gathers across all omitted ordinal dimensions in complete numeric tuple order |
| `cfg.get_all_as<T>("cluster/node/port")` | typed gather with the same matching and ordering contract |

For raw and typed gathers, each specified ordinal is exact and each omitted
ordinal spans every concrete instance. Results compare the complete ordinal
tuple lexicographically, using numeric order at each repeated segment. For
example, `cluster/node[2]/route[2]/port` precedes
`cluster/node[2]/route[10]/port`, and every result under `node[2]` precedes
every result under `node[10]`.

CLI flags address nested instances with one plain ordinal segment per repeated
dimension. Within one argv source, two assignments to the same concrete path
have the same rank and the later token wins. Reversing the tokens reverses the
winner. This does not weaken source-stack precedence: a lower-rank assignment
cannot replace a value supplied by a higher-rank source.

```text
--cluster-node-0-route-2-port=9002
--cluster-node-0-route-2-port=8002
```

In that order, `cluster/node[0]/route[2]/port` resolves to `8002`; reversing
the two tokens resolves it to `9002`.

`get_all` skips instances that lack the sub-path; use `get_all_as` for typed
values. The `config_node` cursor (see [`config_node`](#config_node)) is the
correlation tool when per-instance structure matters.

See [`examples/basics/quickstart.cpp`](../examples/basics/quickstart.cpp) and
[`examples/xml/round_trip.cpp`](../examples/xml/round_trip.cpp).

---

<a id="typed"></a>
## Typed fields: `typed_element<T>`, `register_converter`

`#include "nucleus/schema/converters.h"`

`typed_element<T>` attaches a converter and a type identity to a
`schema_element`; the element is otherwise identical to `element()`.

- `typed_element<T>(name, at)` uses the built-in scalar converter for `T`. Built-in
  converters exist for exactly: `int8_t`, `int16_t`, `int32_t`, `int64_t`,
  `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `float`, `double`, `bool`,
  `char`, `std::string`. Any other type is a compile-time error.
- `typed_element<T>(name, at, conv)` accepts a host converter of type
  `std::function<expected<std::any, std::string>(std::string_view)>`. The
  converter must not throw; return `unexpected(reason)` for any conversion
  error, and wrap exactly a `T` in the returned `std::any`.
- `registered_element<T>(name, at)` declares only the type identity; the
  converter is looked up at load in the builder's converter registry, populated
  via `config_space_builder::register_converter<T>(conv)`. A per-element
  converter overrides the registry's.
- `make_scalar_converter<T>()` is public so a host can compose the built-in
  parsing with extra validation.

Declaring a type is a content contract: every declared path that survives
slicing is converted at the load boundary, and a conversion failure fails the
load loudly (`errc::failed_conversion`), naming the path, the converter's
reason, and the winning layer's label. Absent paths are skipped (absence is
`required`'s concern, not the converter's).

```cpp
if(!builder.register_element(
    nucleus::typed_element<vec3>("pos", nucleus::anchor::keyspace("body"), make_vec3_converter())))
    return 1;
if(!builder.register_element(
    nucleus::typed_element<int32_t>("mass", nucleus::anchor::keyspace("body"))))
    return 1;
// ... load ...
auto pos  = config.get_as<vec3>("body/pos");
auto mass = config.get_as<int32_t>("body/mass");
```

The stored type must equal the type requested from `get_as<T>` outright —
`type_index` equality, no widening, no coercion. Storing `int32_t` and reading
`int64_t` is a type mismatch. A complete custom-converter registration and typed
read-back is in [`examples/schema/typed.cpp`](../examples/schema/typed.cpp).

---

<a id="keying"></a>
## Keying model: primary key, uniqueness, strains

Marking an element with `identity` (via `primary_key_element`) makes its parent
container repeatable: multiple instances ("strains") can coexist in one
document, each distinguished by a distinct primary-key value. Exactly one
primary-key element is allowed per configuration space.

`unique_element` constrains sibling values to be distinct without taking on the
selector role; many unique fields may coexist per container. A primary key is
implicitly unique.

Anonymous instances (no primary-key value) are templates: they compose in
document order and are inherited by all named instances.

Resolution always strips the transient key segment: the resolved keyspace
contains `cluster/server/port`, never `cluster/server/primary/port`. The
primary-key value names which instance was selected, not a permanent path
segment.

Selection is a per-load parameter (`load_options::selection`, below). Rules:

- No selection and exactly one named strain present: auto-resolves to it.
- No selection and multiple named strains: loud load error listing the
  available strain names.
- Unknown selection value: loud error.

Scope policy (`load_options::scope`, a `strain_scope_policy` from
`"nucleus/strain_scope.h"`) governs which entries survive after the slice step,
in terms of Ld (the layer that defined the resolved strain) and Ls (the first
layer introducing a competing strain above Ld):

| Policy | Effect |
|--------|--------|
| `file_level` | The entire keyspace freezes at Ld; every entry whose winning rank exceeds Ld is discarded, keyed and general alike. |
| `space_open_container_closed` | General entries compose freely from all layers; the strain's keyed entries freeze at Ld. **The default.** |
| `container_open_until_next_strain` | The strain's keyed entries compose over [Ld, Ls); general entries are unconstrained. |

Re-opening a named instance across an inheritance layer requires explicit
consent: the derived document marks the instance with an `extend` disposition
(see [Seams you extend](api-extending.md#inheritance)); re-declaring it without
`extend` is rejected as a duplicate.

```cpp
if(!builder.register_element(
    nucleus::element("server", nucleus::anchor::keyspace("cluster"))))
    return 1;
if(!builder.register_element(
    nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))))
    return 1;
if(!builder.register_element(
    nucleus::unique_element("serial", nucleus::anchor::keyspace("cluster/server"))))
    return 1;
```

See [`examples/schema/strains.cpp`](../examples/schema/strains.cpp).

---

<a id="tree-references"></a>
## Tree references and pkey shortcuts

Values may embed `${...}` tokens that are resolved at load time by pass-2 after
slicing. Three families of token exist:

### `${abs:path}` and `${rel:path}` — absolute and relative references

`${abs:cluster/port}` resolves to the value at the exact keyspace path
`cluster/port`. `${rel:./sibling}` resolves relative to the containing scope of
the value (parent path + segment), and `${rel:../other}` walks up one level
before descending. The `??` operator chains arms left-to-right; the first
present value wins; a quoted literal is the fallback floor:

```
app/label = ${abs:cluster/port ?? "8080"}
server/url = ${rel:./host}:${rel:./port}
```

See [`examples/references/tree_references.cpp`](../examples/references/tree_references.cpp) for live
demonstrations of `abs:`, `rel:`, `??`, and `${dir.path}`.

### `${node.field}` — auto-named pkey tokenizer

When a schema declares a primary-key element under a container, the engine
auto-registers a tree-access tokenizer named after the **container's tag**.
For a schema that declares:

```cpp
register_element(element("server", anchor::keyspace("cluster")));
register_element(primary_key_element("name", anchor::keyspace("cluster/server")));
register_element(element("endpoint", anchor::keyspace("cluster/server")));
```

the token `${server.name}` resolves to the **selected strain's** `name` field,
and `${server.endpoint}` resolves to its `endpoint` field. The token is
pkey-anchored: selecting "primary" does not leak "secondary"'s fields, even
when both strains exist in the same source.

- **Field surface:** only the pkey element's own sibling leaves are
  reachable via `${server.*}`. Deeper subtrees or other containers use
  `${abs:...}` or `${rel:...}`.
- **Zero-instance diagnostic:** if the schema declares the pkey element
  but no instance is in scope (optional pkey, zero strains selected),
  `${server.name}` fails with the precise message
  `"${server.name} requires a selected primary-key instance; this configuration
  has none in scope"`. The `??` operator catches this and falls through to the
  next arm.
- **Reserved-name protection:** a pkey container whose tag collides with
  a reserved category name (`env`, `string`, `abs`, `rel`, `scope`, `file`,
  `dir`, `self`) is rejected at `register_element` time with
  `errc::rejected_registration`. Rename the schema element.
- **Host shadowing:** a host that calls `install_tree_tokenizer` with the
  same category name before `build()` shadows the built-in auto-named tokenizer
  for that category (last-registration-wins). See
  [Tree-access tokenizers](api-extending.md#tree-tokenizers) for the host API.

```cpp
// The token in the source value:
//   cluster/server/primary/description = "${server.name} at ${server.endpoint}"
// After load with selection="primary":
//   cfg.get("cluster/server/description") == "primary at 10.0.0.1:9000"
```

See [`examples/tokens/pkey_tokenizer.cpp`](../examples/tokens/pkey_tokenizer.cpp).

### Multiplicity and path model

**Templating via anonymous-instance inheritance.** A config element that
carries no primary-key value is a template: it composes into all named
instances in document order. Named instances inherit the template's fields and
may override specific ones.

**`inherit=` specialization.** A derived XML document marks a named instance
with `extend` to re-open it across the inheritance chain; without `extend`, a
duplicate named instance is a layering violation.

**`<include>` composition.** Explicit composition of multiple config fragments
into one document is deferred. Use the inheritance chain
(`document_paths` + `make_document`) for multi-file loading today.

**File-relative paths via `${dir.path}`.** Each document in an inheritance
chain resolves `${dir.path}` to its own file's directory, so a base document
and a derived document each resolve to their own location. This is the safe
pattern for composing relative filesystem paths (e.g.
`${dir.path}/certs/ca.pem`). The per-source binding is demonstrated in
[`examples/references/tree_references.cpp`](../examples/references/tree_references.cpp) (see the
`dir.path` note) and proved per-file in
[`tests/location_token_wiring_test.cpp`](../tests/location_token_wiring_test.cpp).

---

<a id="space"></a>
## `config_space` — the sealed space

`#include "nucleus/config_space.h"`

The immutable product of `build()`. Its surface is read-only — registration on a
sealed space is impossible by construction. It is copyable (a deep copy; no
shared state links two spaces) and freely thread-readable: `load_config()` borrows it
by const reference.

```cpp
std::size_t schema_count() const noexcept;
std::size_t tokenizer_count() const noexcept;   // includes the auto-installed core tokenizers
std::size_t converter_count() const noexcept;
std::vector<conflict_report> conflicts() const;
std::string_view space_name() const noexcept;   // the name set via config_space_builder::name(); empty if unnamed
expected<std::string, error> generate_completion(shell which, std::string_view prog,
                                const cli_delimiter &delimiter = {},
                                const key_path &anchor = {},
                                std::string_view space_name = {}) const;
std::span<const schema_element> schema_elements() const;  // the declared schema, for emitters/derivation
config_space_builder expand() const;  // a NEW builder seeded with a deep copy of this space
```

Two free functions derive from a sealed space:

```cpp
key_recognizer recognizer_of(const config_space &space);
```

returns the schema-surface predicate an `argv_source` uses to reject undeclared
flags (the closure borrows the space; keep the space alive). See
[`examples/cli/argv_recognizer.cpp`](../examples/cli/argv_recognizer.cpp).

---

<a id="stack"></a>
## Composing sources: `source_stack`

`#include "nucleus/config_source/source_stack.h"`

The explicit, ordered set of sources handed to `load_config()`. **Order is
precedence**: a later-listed source overlays an earlier-listed one. Sources are
moved in and type-erased into `source_handle`s; the stack owns them.

```cpp
template <config_source... Ss> explicit source_stack(Ss... sources);
source_stack &push_back(source_handle h);
std::span<source_handle> layers() noexcept;
std::size_t size() const noexcept;
bool empty() const noexcept;
```

```cpp
nucleus::runtime_source defaults;            // lowest precedence
defaults.set("server/host", "localhost").set("server/port", "8080");

nucleus::env_source env;                     // overrides defaults
env.set("server/host", "staging-host");

nucleus::argv_source argv(std::vector<std::string>{"--server-port=9090"});
argv.recognize_with(nucleus::recognizer_of(space));   // highest precedence

auto loaded = nucleus::load_config(space,
    nucleus::source_stack{std::move(defaults), std::move(env), std::move(argv)},
    {});
```

The shipped sources — `xml_source`, `env_source`, `argv_source`,
`runtime_source` — are documented in
[Shipped implementations](api-implementations.md); any plain struct satisfying
the [source concept](api-extending.md#source_concept) composes the same way.
See [`examples/composition/source_stack.cpp`](../examples/composition/source_stack.cpp).

---

<a id="load"></a>
## `load_config()` and `load_options`

`#include "nucleus/config_space.h"`

```cpp
load_result load_config(const config_space &space,
                        source_stack &stack,
                        const load_options &options = {});
load_result load_config(const config_space &space,
                        source_stack &&stack,         // inline composition: source_stack{...}
                        const load_options &options = {});
// load_result = expected<config, error>

struct load_options {
    std::optional<std::string>                        selection;   // strain to select
    strain_scope_policy                               scope = strain_scope_policy::space_open_container_closed;
    inherit_policy                                    inherit;     // chain admissibility + depth cap (default 16)
    std::vector<std::string>                          document_paths;
    std::function<source_handle(const std::string &)> make_document;
    std::size_t                                       reference_budget = 0;  // max tree-reference substitutions per pass-2 resolve; 0 = engine default (10000)
    std::size_t                                       expansion_budget = 0;  // max token-expansion substitutions per pass-1 fold; 0 = engine default (2500)
    log_sink*                                         log = nullptr;         // optional host sink for load-time warnings; nullptr = no logging
};
```

`load_config` is the one entry point. In order it:

1. expands `document_paths` through `make_document` (the host's "path → source"
   decision) and walks each document's inheritance chain (base documents first);
2. **auto-gates capabilities**: the schema's shape derives its requirements and
   the assembled stack is gated before any fold — a hard shortfall is a loud
   named error, a soft one degrades observably (no host call required);
3. folds all layers onto one keyspace, expanding `${...}` tokens per source as
   it goes — documents (and their inheritance chains) occupy the lowest ranks,
   then the stack's sources in stack order above them, so a stack source
   overrides document content;
4. slices the selected strain, validates against the schema, converts typed
   paths, and freezes the result into an immutable `config`.

The stack is borrowed, not consumed: it stays valid afterward, so the same
stack can pre-flight via `check_capabilities()` and then load, or load more
than once. Any failure — a source pull error, a gate refusal, a token error, a
schema or conversion violation — returns an [`error`](#expected) whose `code`
names the failing pipeline stage and whose `message` carries the verbatim
reason.

```cpp
const char *document = R"(<server host="127.0.0.1" mode="http"/>)";
auto make = [document](const std::string &) -> nucleus::source_handle {
    return nucleus::source_handle(
        nucleus::xml_source::from(
            nucleus::xml_source_options::of_string(document)));
};

auto loaded = nucleus::load_config(space,
    nucleus::source_stack{std::move(argv)},
    nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make});
```

See [`examples/xml/xml.cpp`](../examples/xml/xml.cpp) (documents under argv),
[`examples/schema/strains.cpp`](../examples/schema/strains.cpp) (selection), and
[`examples/composition/layering.cpp`](../examples/composition/layering.cpp) (precedence and
provenance).

---

<a id="preflight"></a>
## Capability pre-flight: `check_capabilities`

`#include "nucleus/config_space.h"`

```cpp
gate_result check_capabilities(const config_space &space,
                               const source_stack &stack,
                               const load_options &options = {});
```

Runs the same capability gate `load_config()` runs, without pulling or folding, so a
host can validate fit ahead of a load; the pre-flight and the load never
disagree. The stack is borrowed const — it stays intact for the `load_config()` that
follows it. `gate_result` is `expected<gated_features, error>` — the honored
capabilities plus the observable degradations, or the hard-shortfall error
(`errc::unmet_capability`).

The derivation is shape-driven (`"nucleus/schema/capability_requirements.h"`,
`derive_capability_requirements(std::span<const schema_element>)`): any
non-root element requires `nesting` (hard), any repeated element requires
`duplicate_keys` (hard), any typed element requests `typed_scalars` (soft). A
hard capability is satisfied when ANY layer in the stack provides it. See
[`examples/sources/capability_gating.cpp`](../examples/sources/capability_gating.cpp).

---

<a id="configuration"></a>
## `config` — the resolved result

`#include "nucleus/config.h"`

The immutable, self-owning output of a load. Every value is copied out into an
owned string at the load boundary and the source buffers are dropped, so it
outlives every source and is freely thread-safe to read.

Normal resolved configurations come from `load_config()`. Low-level code that
already owns a raw path/value map can use the checked factory:

```cpp
static expected<config, error> config::from_values(
    std::map<std::string, std::string> values,
    provenance origins = {});
```

The factory returns `errc::malformed_source` for a malformed key path and
`errc::schema_violation` for a conflicting storage shape. It never returns an
invalid `config`; the unchecked map constructors are internal to resolution.

Repeated paths (both repeated containers and repeated leaves) are stored as
indexed scalars in the internal map: `"config/tags[0]"="a"`,
`"cluster/node[0]/port"="80"`, `"cluster/node[1]/port"="90"`.
Every concrete instance of one canonical leaf agrees on which path segments
are indexed. A plain leaf and indexed instances of that same leaf cannot
coexist: `tags` conflicts with `tags[0]`, and a plain `route` leaf in one node
conflicts with an indexed `route[0]` leaf in another. `load_config()` and
`config::from_values()` reject that state before a `config` is constructed.

```cpp
std::optional<std::string> get(const std::string &key) const;
std::vector<std::string> get_all(const std::string &key) const;
bool contains(const std::string &key) const;
const origin *provenance_of(const std::string &key) const;   // "why is this value X?"
template<typename T> expected<T, error> get_as(const std::string &key) const;
template<typename T> expected<std::vector<T>, error> get_all_as(const std::string &key) const;
config_node root() const noexcept;   // entry point for the walk API
std::size_t size() const noexcept;
bool empty() const noexcept;
std::vector<std::string> keys() const;
```

- `get("cluster/node[0]/port")` — scalar at an exact indexed path.
- `get("cluster/node/port")` — returns `nullopt` when the path crosses a
  repeated container without an index.
- `get_all("cluster/node/port")` — gathers across all omitted ordinal
  dimensions in complete numeric tuple order; returns a one-element vector for
  non-repeated paths.
- `get_as<T>("cluster/node[0]/port")` — typed value at an exact indexed path.
- `get_as<T>("cluster/node/port")` — returns `errc::index_required` naming
  the container and instance count when the path crosses a repeated container.
- `get_all_as<T>("cluster/node/port")` — typed gather with the same segment
  matching and complete ordinal-tuple ordering as `get_all`.

Gather paths may qualify any subset of repeated dimensions. Every explicit
ordinal is an exact constraint; every omitted ordinal fans out across all
concrete instances. The raw and typed calls return the same sequence:

```cpp
// The fixture contains outer and inner ordinals 0, 1, 2, and 10. Each port is
// encoded as outer * 100 + inner.
cfg.get_all("cluster/node/route/port");
// {"0", "1", "2", "10", "100", "101", "102", "110",
//  "200", "201", "202", "210", "1000", "1001", "1002", "1010"}

cfg.get_all("cluster/node/route[10]/port");
// {"10", "110", "210", "1010"} -- inner ordinal fixed

cfg.get_all("cluster/node[2]/route/port");
// {"200", "201", "202", "210"} -- outer fixed; route[2] precedes route[10]

cfg.get_all("cluster/node[2]/route[10]/port");
// {"210"} -- fully qualified
```

`get_all_as<std::int32_t>` produces the corresponding integer vectors for
these paths. Numeric tuple comparison applies at every repeated segment; it
does not stop at the first repeated ancestor.

The typed reads return `expected<T, error>`; branch on the `code`, print the
whole `error` (or its `message`) for humans:

| Condition | `error.code` | `error.message` says |
|-----------|--------------|----------------------|
| path carries no value | `errc::absent_key` | the path is absent |
| path crosses a repeated container without an index | `errc::index_required` | names the container and instance count |
| path has a value but no converter | `errc::missing_converter` | the path declares no type converter |
| stored type does not equal requested type | `errc::mismatched_type` | type mismatch for the path |

Branching on the code is the supported host pattern:

```cpp
auto port = config.get_as<int32_t>("server/port");
int32_t resolved_port = 8080;                      // the fallback default
if(port)
    resolved_port = port.value();
else if(port.error().code != nucleus::errc::absent_key)
{
    std::cerr << port.error() << '\n';             // e.g. "mismatched_type: type mismatch for ..."
    return 1;
}
```

See [`examples/schema/typed.cpp`](../examples/schema/typed.cpp) and
[`examples/composition/reusable_space.cpp`](../examples/composition/reusable_space.cpp).

---

<a id="config_node"></a>
## `config_node` — the walk API

`#include "nucleus/config_node.h"` (included transitively by `"nucleus/config.h"`)

A value-semantic cursor into the resolved configuration tree. It holds a `const`
pointer to the immutable `config` and an owned path string. It is copyable and
never exposes internal storage. Its lifetime is tied to the `config` it was
derived from.

**Entry point:** `config::root()` returns a root-anchored cursor.

```cpp
const nucleus::config &cfg = loaded.value();
nucleus::config_node root = cfg.root();
```

**Navigation** — never fails loudly. A missing child or out-of-range index
yields a null-view that propagates through further navigation. Terminal
`as<T>()` returns `errc::absent_key` carrying the full attempted path.

```cpp
// Chained navigation: cluster -> node (repeated) -> [0] -> port.
auto port = cfg.root()["cluster"]["node"][0]["port"].as<std::string>();
// port is expected<std::string, error>; check before use.
if(port)
    std::cout << *port << '\n';
```

**Null-view chaining** — navigation through an absent key stays invalid:

```cpp
cfg.root()["nonexistent"]["child"].exists()  // false
cfg.root()["nonexistent"][0].exists()        // false
```

**Shape queries:**

```cpp
bool     exists() const noexcept;          // true when the path exists in the config
node_kind kind() const noexcept;           // scalar | container | repeated
std::size_t count() const noexcept;        // instance count for repeated; 1 for scalar/container; 0 for absent
std::vector<config_node> children() const; // ordinal order for repeated; canonical order for container
std::string_view path() const noexcept;    // the key path that identifies this node
std::optional<std::string> value() const;  // raw string for scalars; nullopt for containers/absent
```

`node_kind` values: `node_kind::scalar`, `node_kind::container`,
`node_kind::repeated`.

**Typed read** — delegates to `config::get_as<T>()`:

```cpp
template<typename T>
expected<T, error> as() const;
```

`as<std::string>()` returns the raw scalar value directly without requiring a
registered converter.

**Example — iterate all ports across a repeated container:**

```cpp
auto nodes = cfg.root()["cluster"]["node"];
if(nodes.kind() == nucleus::node_kind::repeated)
{
    for(const nucleus::config_node &instance : nodes.children())
    {
        auto port = instance["port"].as<std::string>();
        if(port)
            std::cout << instance.path() << "/port = " << *port << '\n';
    }
}
```

**Pre-order visit** — `visit(fn)` calls `fn` at each node. Returning `false` at
any depth cancels the entire remaining visit from that anchor, including later
siblings. Repeated instances are visited in numeric ordinal order; distinct
sibling container fields in canonical order.

```cpp
cfg.root()["cluster"]["node"].visit([](const nucleus::config_node &n) {
    std::cout << n.path() << '\n';
    return true;   // continue
});
```

**Enter/leave walk** — `walk(walker)` has a deliberately different cancellation
contract. It provides nesting-aware callbacks through a `config_tree_walker`
subclass. `enter()` returning `false` prunes only that node's descendants;
later siblings are still visited, and `leave()` is still called for the pruned
node and on every later ascent.

```cpp
struct port_collector : nucleus::config_tree_walker
{
    std::vector<std::string> ports;

    bool enter(const nucleus::config_node &n) override
    {
        if(n.path().ends_with("/port"))
            if(auto v = n.value(); v)
                ports.push_back(*v);
        return true;
    }
    void leave(const nucleus::config_node &) override {}
};

port_collector collector;
cfg.root().walk(collector);
```

See [`examples/basics/quickstart.cpp`](../examples/basics/quickstart.cpp) (Part 2 repeated
container demo), [`tests/config_node_test.cpp`](../tests/config_node_test.cpp).

---

<a id="provenance"></a>
## Provenance: `origin`

`#include "nucleus/keyspace/provenance.h"`

`provenance_of(key)` returns the `origin` of the value that won, or `nullptr`.

```cpp
struct origin {
    std::size_t rank;        // precedence rank of the winning layer
    std::string layer;       // label: "stack[N]" for stack sources, "path:<path>" for documents
    owner_token owner;       // opaque token of the winning source
    std::optional<std::size_t> inheritance_layer;  // position within an inheritance chain, if any
};
```

See [`examples/composition/layering.cpp`](../examples/composition/layering.cpp).

---

<a id="key_path"></a>
## `key_path` — addressing the keyspace

`#include "nucleus/keyspace/key_path.h"`

A decomposed `/`-separated path. Construct from text via `parse` (fallible) or
from segments.

```cpp
static expected<key_path, std::string> key_path::parse(std::string_view text);
explicit key_path(std::vector<std::string> segments);

bool empty() const noexcept;
std::size_t size() const noexcept;
const std::vector<std::string> &segments() const noexcept;
const std::string &front() const;
const std::string &leaf() const;
key_path parent() const;                    // a/b/c -> a/b
std::string str() const;                    // canonical "/"-joined form

static bool is_indexed_segment(std::string_view seg) noexcept;  // "node[0]" -> true
static std::string_view base_name(std::string_view seg) noexcept; // "node[0]" -> "node"
static std::uint64_t ordinal_of(std::string_view seg) noexcept;  // "node[0]" -> 0
```

`parse` rejects leading/trailing separators and empty segments. A segment
containing `[` must be a valid indexed segment: non-empty base name, decimal
ordinal with no leading zeros (except a lone `0`), no greater than
`key_path::max_ordinal`, closing `]`. That bound is the largest value
`std::size_t` holds losslessly on every supported platform, so an ordinal beyond
it is reported as a malformed path rather than narrowed to fit.
Element names must not start with a digit — this keeps the CLI bijection
invertible (a digit-led segment after a repeated container is unambiguously an
ordinal). Most schema code can skip `key_path` entirely and pass a string to
`anchor::keyspace`.

---

<a id="emit"></a>
## Emitting: templates and documents

`#include "nucleus/config_emitter.h"` (the concept)

Output is the inverse of a source: a sealed space's declared schema projects
into a blank document template, and a resolved configuration renders back out.
The format-agnostic `config_emitter` concept is defined by owned results, and
rendering completes validation and serialization before any destination is
involved:

```cpp
template<typename Emitter>
concept config_emitter = requires(const Emitter e, const config_space &space,
                                  const config &config) {
    { e.render_template(space) }
        -> std::same_as<expected<std::string, error>>;
    { e.render_document(config, space) }
        -> std::same_as<expected<std::string, error>>;
};
```

Each shipped format exposes the pair as free functions in its own namespace,
plus a `struct emitter` modeling the concept:

| Format | Header | Owned free functions | CMake target |
|--------|--------|----------------------|--------------|
| XML  | `"nucleus/xml/xml_emitter.h"`   | `nucleus::xml::render_template` / `render_document`  | `nucleus::xml` |
| env  | `"nucleus/env/env_emitter.h"`   | `nucleus::env::render_template` / `render_document`  | `nucleus::env` |
| argv | `"nucleus/argv/argv_emitter.h"` | `nucleus::argv::render_template` / `render_document` | `nucleus::argv` |

```cpp
auto xml = nucleus::xml::render_document(config, space);
auto env = nucleus::env::render_document(config);
auto argv = nucleus::argv::render_document(config);

if(!xml)
    return 1;
std::cout << xml.value();
```

Ordinary XML document rendering is schema-aware: pass the same sealed space
used to load the configuration. It checks every concrete path and repeated role
before producing XML. The weaker `render_document_schema_blind(config)`
operation validates only universal XML representability and is deliberately
explicit at the call site. Checked delivery has the same distinction:

```cpp
auto delivered = nucleus::xml::emit_document(config, space, std::cout);
auto weak = nucleus::xml::emit_document_schema_blind(config, std::cout);
```

The owned artifact preserves semantic state, not source formatting: reloading
with the same sealed schema and format settings preserves the exact concrete key
set and stored strings. Built-in deterministic converters reproduce typed values;
arbitrary host-converter determinism is not promised. Provenance and capability
degradations are recomputed by the new load rather than serialized. XML preserves
the complete validated subtree; flat argv and environment output preserve
concrete repeated paths as replayable overlays over the same structural base,
spelled and anchored only as
[CLI Grammar and Multi-Space Addressing](cli-grammar.md#ordinal-segment-rule--repeated-containers)
defines.

The `emit_*` conveniences deliver the finished artifact to the destination's
stream buffer, and succeed only when that stream buffer accepted every byte of
it; a prefailed stream, a null or zero-accepting buffer, or a short write
returns `errc::unwritable_destination`. That code is a delivery failure; a
configuration the format cannot represent is rejected earlier by the renderer as
`errc::malformed_source`. An emitter never flushes: a later flush, close, or
persistence failure is the caller's and lies outside the emitter result, and the
prefix a short write already accepted is not rolled back. A repeated container
renders N sibling elements in XML or N indexed entries in env/argv, in numeric
ordinal order. See
[`examples/xml/round_trip.cpp`](../examples/xml/round_trip.cpp),
[`examples/xml/xml_persist.cpp`](../examples/xml/xml_persist.cpp), and
[`examples/xml/emit_template.cpp`](../examples/xml/emit_template.cpp).

---

<a id="expected"></a>
## `expected<T, E>` and `error` — fallible returns

`#include "nucleus/expected.h"` and `"nucleus/error.h"`

`expected<T, E>` is the fallible-return vocabulary used across the public API.
It mirrors `std::expected` (C++23); a future migration points the aliases at
the standard type and edits nothing else. Truthy when it holds a value;
construct the error alternative with `nucleus::unexpected(e)`. Check every
result — a load, a registration — rather than discarding it.

Every public result channel carries `nucleus::error` as its `E`:

```cpp
enum class errc {
    unreadable_source, malformed_source, unwritable_destination,
    invalid_inheritance, unmet_capability, layering_violation, unresolved_token,
    invalid_selection, schema_violation, failed_conversion,
    rejected_registration, sealed_builder, absent_key, index_required,
    missing_converter, mismatched_type, ambiguous_result,
};
constexpr std::string_view to_string(errc code) noexcept;

struct error {
    errc        code;       // machine-readable: branch on this
    std::string message;    // verbatim human-readable reason
};
std::string to_string(const error &e);                      // "code: message"
std::ostream &operator<<(std::ostream &os, const error &e); // same rendering
```

The codes follow the load pipeline (source → inheritance → gate → fold →
tokens → selection → schema → conversion), then registration, then typed
reads; `unwritable_destination` is the one output-side code, reported when a
destination will not accept a rendered artifact. A host branches on `code`; the human detail travels in `message`.
Host-supplied seams (converters, the registration policy verdict) still
traffic in plain reason strings — the engine attaches the code at the seam
where the failure class is known.

```cpp
auto loaded = nucleus::load_config(space, stack, {});
if(!loaded)
{
    std::cerr << loaded.error() << '\n';   // streams "code: message"
    if(loaded.error().code == nucleus::errc::invalid_selection)
        print_available_strains();
    return 1;
}
const nucleus::config &config = loaded.value();
```

---

<a id="owner_token"></a>
## `owner_token` — opaque identity

`#include "nucleus/identity.h"`

An opaque tag a host attaches to a registration. The core stores and surfaces it
(in conflict reports and provenance) but never interprets it. Default-constructed
tokens are anonymous and each is distinct; a token wrapping a value compares
equal to another wrapping the same type and value.

```cpp
owner_token();              // anonymous, distinct
explicit owner_token(T value);  // typed; equality by wrapped type + value
bool has_value() const noexcept;
```

---

<a id="diagnostics"></a>
## Diagnostics: `suggest_keys`, `conflict_report`

### `suggest_keys`

`#include "nucleus/diagnostics/key_suggester.h"` — "did you mean...?" over a set
of known keys. The load uses it automatically when rejecting an undeclared key.

```cpp
std::vector<std::string> suggest_keys(std::string_view unknown,
                                      std::span<const std::string> known,
                                      std::size_t limit = 3);
```

### `conflict_report`

`#include "nucleus/diagnostics/conflict_report.h"` — returned by
`config_space_builder::conflicts()` (and by the sealed space, which
carries the ledger forward). Two registrations claiming the same key path
produce one report that names every claimant and refuses to pick a winner;
adjudication is the host's.

```cpp
const std::string &key_path() const;
const std::vector<claimant> &claimants() const;
std::size_t size() const;
std::string describe() const;
```

See [`examples/basics/diagnostics.cpp`](../examples/basics/diagnostics.cpp).

---

<a id="completion"></a>
## Completion: `generate_completion`, `shell`

`#include "nucleus/completion/completion.h"` (for the `shell` enum)

Projects the registered schema into a static shell completion script through the
same flag mapping the CLI surface uses, so completion cannot drift from the CLI.

```cpp
enum class shell { bash, zsh };
expected<std::string, error> config_space::generate_completion(
                                                     shell which, std::string_view prog,
                                                     const cli_delimiter &delimiter = {},
                                                     const key_path &anchor = {},
                                                     std::string_view space_name = {}) const;
```

A host that re-delimits its CLI (`argv_source::delimit_with`) or anchors it
(`argv_source::anchor_at`) passes the same `cli_delimiter` and anchor here,
keeping the completed flags identical to the parsed ones. When `space_name` is
non-empty, every completion entry is prefixed with the space name, matching a
`multispace_argv_source` that routes tokens by their first segment.
An `enum_element`'s value set becomes that flag's completion candidates. A pure
read of the sealed schema. nucleus is a library, not a CLI — it returns the
script and the host decides how to surface it.

`prog` reaches shell command position unquoted in the generated script, so it must
be a bare command token: it opens with a letter, a digit or `_`, and carries only
letters, digits, `.`, `_` and `-`. A name carrying a path separator, whitespace, a
newline or any shell metacharacter is refused with `errc::malformed_source` and no
script text is produced, as is a name opening with `-` (the shell would read it as
an option of the command the name was meant to occupy) or with `.` (a path
reference rather than a command). See
[`examples/cli/completion.cpp`](../examples/cli/completion.cpp) and
[`examples/cli/argv_delimiter.cpp`](../examples/cli/argv_delimiter.cpp).
