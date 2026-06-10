# Types you use

The host-side vocabulary: the types a program instantiates, passes in, and reads
back. None of these requires subclassing. For the seams a host extends, see
[Seams you extend](api-extending.md).

## Contents

- [The lifecycle at a glance](#lifecycle)
- [`configuration_space_builder` — registration](#builder)
- [Declaring a schema: `schema_element`, `anchor`, free factories](#schema)
- [Typed fields: `typed_element<T>`, `register_converter`](#typed)
- [Keying model: primary key, uniqueness, strains](#keying)
- [`configuration_space` — the sealed space](#space)
- [Composing sources: `source_stack`](#stack)
- [`load()` and `load_options`](#load)
- [Capability pre-flight: `check_capabilities`](#preflight)
- [`configuration` — the resolved result](#configuration)
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
configuration_space_builder  --register_* / install_tokenizer-->  build()
        |
        v
configuration_space (sealed, immutable, reusable)
        |
        v
nucleus::load(space, source_stack, load_options)  -->  expected<configuration, error>
```

A builder accepts registrations and `build()` seals it into an immutable
`configuration_space`. The free function `nucleus::load` folds an explicitly
composed `source_stack` against the sealed space and yields an immutable
`configuration`. The space is never modified by a load, so one space serves many
loads — even concurrently (see [`examples/reusable_space.cpp`](../examples/reusable_space.cpp)).
The stack is borrowed by the load, not consumed, so one stack serves many loads
too. Every fallible step reports a typed [`error`](#expected): an `errc` code a
program branches on plus a verbatim human-readable message.

---

<a id="builder"></a>
## `configuration_space_builder` — registration

`#include "nucleus/configuration_space.h"`

The mutable front end. It owns the schema, tokenizer, and converter registries
plus the host registration policy and the claim/conflict ledger. Move-only.

```cpp
configuration_space_builder builder;

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

std::size_t schema_count() const noexcept;
std::size_t tokenizer_count() const noexcept;
std::size_t converter_count() const noexcept;
std::vector<conflict_report> conflicts() const;

configuration_space build();   // seals; the builder is spent afterwards
```

- `registration_result` is `expected<void, error>`: truthy on success. On
  failure the code is `errc::rejected_registration` (the registration policy's
  reason, verbatim, in `message`) or `errc::sealed_builder`.
- `expected` is `[[nodiscard]]`, so silently dropping a registration result is a
  compiler warning — check each one, `if(!builder.register_element(...)) ...`.
- Every registration carries an opaque `owner_token` and is first offered to the
  [registration policy](api-extending.md#registration_policy).
- After `build()`, every `register_*` / `install_*` call is a loud state-machine
  error (`errc::sealed_builder`), never a silent no-op.

See [`examples/quickstart.cpp`](../examples/quickstart.cpp).

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
    bool repeated = false;                   // keeps ALL N same-named values as a collection
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

See [`examples/schema.cpp`](../examples/schema.cpp).

### Repeated elements

A `repeated_element` is a leaf field that keeps ALL N occurrences of a
same-named entry as one ordered collection — distinct from keyed containers
(instances distinguished by the primary key) and template merging (anonymous
instances composing). An element cannot be both `repeated` and `identity`, nor
`repeated` and `unique`. Feeding a repeated element requires a source layer with
the `duplicate_keys` capability (XML or the runtime source); within one source
layer occurrences accumulate in document order, and a higher-precedence layer
replaces the collection wholesale. `get_all()` / `get_all_as<T>()` return values
in fold order, and `collection_provenance_of()` returns one origin per element
in the same order. See [`examples/round_trip.cpp`](../examples/round_trip.cpp).

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
  via `configuration_space_builder::register_converter<T>(conv)`. A per-element
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
read-back is in [`examples/typed.cpp`](../examples/typed.cpp).

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

See [`examples/strains.cpp`](../examples/strains.cpp).

---

<a id="space"></a>
## `configuration_space` — the sealed space

`#include "nucleus/configuration_space.h"`

The immutable product of `build()`. Its surface is read-only — registration on a
sealed space is impossible by construction. It is copyable (a deep copy; no
shared state links two spaces) and freely thread-readable: `load()` borrows it
by const reference.

```cpp
std::size_t schema_count() const noexcept;
std::size_t tokenizer_count() const noexcept;   // includes the auto-installed core tokenizers
std::size_t converter_count() const noexcept;
std::vector<conflict_report> conflicts() const;
std::string generate_completion(shell which, std::string_view prog,
                                const cli_delimiter &delimiter = {}) const;
std::span<const schema_element> schema_elements() const;  // the declared schema, for emitters/derivation
configuration_space_builder expand() const;  // a NEW builder seeded with a deep copy of this space
```

Two free functions derive from a sealed space:

```cpp
key_recognizer recognizer_of(const configuration_space &space);
```

returns the schema-surface predicate an `argv_source` uses to reject undeclared
flags (the closure borrows the space; keep the space alive). See
[`examples/argv_recognizer.cpp`](../examples/argv_recognizer.cpp).

---

<a id="stack"></a>
## Composing sources: `source_stack`

`#include "nucleus/configuration_source/source_stack.h"`

The explicit, ordered set of sources handed to `load()`. **Order is
precedence**: a later-listed source overlays an earlier-listed one. Sources are
moved in and type-erased into `source_handle`s; the stack owns them.

```cpp
template <configuration_source... Ss> explicit source_stack(Ss... sources);
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

auto loaded = nucleus::load(space,
    nucleus::source_stack{std::move(defaults), std::move(env), std::move(argv)},
    {});
```

The shipped sources — `xml_source`, `env_source`, `argv_source`,
`runtime_source` — are documented in
[Shipped implementations](api-implementations.md); any plain struct satisfying
the [source concept](api-extending.md#source_concept) composes the same way.
See [`examples/source_stack.cpp`](../examples/source_stack.cpp).

---

<a id="load"></a>
## `load()` and `load_options`

`#include "nucleus/configuration_space.h"`

```cpp
load_result load(const configuration_space &space,
                 source_stack &stack,
                 const load_options &options = {});
load_result load(const configuration_space &space,
                 source_stack &&stack,         // inline composition: source_stack{...}
                 const load_options &options = {});
// load_result = expected<configuration, error>

struct load_options {
    std::optional<std::string>                        selection;   // strain to select
    strain_scope_policy                               scope = strain_scope_policy::space_open_container_closed;
    inherit_policy                                    inherit;     // chain admissibility + depth cap (default 16)
    std::vector<std::string>                          document_paths;
    std::function<source_handle(const std::string &)> make_document;
};
```

`load` is the one entry point. In order it:

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
   paths, and freezes the result into an immutable `configuration`.

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

auto loaded = nucleus::load(space,
    nucleus::source_stack{std::move(argv)},
    nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make});
```

See [`examples/xml.cpp`](../examples/xml.cpp) (documents under argv),
[`examples/strains.cpp`](../examples/strains.cpp) (selection), and
[`examples/layering.cpp`](../examples/layering.cpp) (precedence and
provenance).

---

<a id="preflight"></a>
## Capability pre-flight: `check_capabilities`

`#include "nucleus/configuration_space.h"`

```cpp
gate_result check_capabilities(const configuration_space &space,
                               const source_stack &stack,
                               const load_options &options = {});
```

Runs the same capability gate `load()` runs, without pulling or folding, so a
host can validate fit ahead of a load; the pre-flight and the load never
disagree. The stack is borrowed const — it stays intact for the `load()` that
follows it. `gate_result` is `expected<gated_features, error>` — the honored
capabilities plus the observable degradations, or the hard-shortfall error
(`errc::unmet_capability`).

The derivation is shape-driven (`"nucleus/schema/capability_requirements.h"`,
`derive_capability_requirements(std::span<const schema_element>)`): any
non-root element requires `nesting` (hard), any repeated element requires
`duplicate_keys` (hard), any typed element requests `typed_scalars` (soft). A
hard capability is satisfied when ANY layer in the stack provides it. See
[`examples/capability_gating.cpp`](../examples/capability_gating.cpp).

---

<a id="configuration"></a>
## `configuration` — the resolved result

`#include "nucleus/configuration.h"`

The immutable, self-owning output of a load. Every value is copied out into an
owned string at the load boundary and the source buffers are dropped, so it
outlives every source and is freely thread-safe to read.

```cpp
std::optional<std::string> get(const std::string &key) const;
std::vector<std::string> get_all(const std::string &key) const;   // repeated paths: the full collection
bool contains(const std::string &key) const;
const origin *provenance_of(const std::string &key) const;        // "why is this value X?"
const std::vector<origin> *collection_provenance_of(const std::string &key) const;
template<typename T> expected<T, error> get_as(const std::string &key) const;
template<typename T> expected<std::vector<T>, error> get_all_as(const std::string &key) const;
std::size_t size() const noexcept;
bool empty() const noexcept;
std::vector<std::string> keys() const;   // canonical order, repeated paths once
```

For a path declared `repeated`, `get_all()` returns the full ordered collection
and `get()` returns the last value; for a single-value path `get_all()` returns
a one-element vector. `provenance_of()` covers scalar keys only; a collection's
per-element origins come from `collection_provenance_of()`.

The typed reads return `expected<T, error>`; branch on the `code`, print the
whole `error` (or its `message`) for humans:

| Condition | `error.code` | `error.message` says |
|-----------|--------------|----------------------|
| path carries no value | `errc::absent_key` | the path is absent |
| path has a value but no converter | `errc::missing_converter` | the path declares no type converter |
| stored type does not equal requested type | `errc::mismatched_type` | type mismatch for the path |
| path holds a typed collection — use `get_all_as` | `errc::mismatched_type` | use `get_all_as<T>()` |
| path holds a single typed value — use `get_as` | `errc::mismatched_type` | use `get_as<T>()` |

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

See [`examples/typed.cpp`](../examples/typed.cpp) and
[`examples/reusable_space.cpp`](../examples/reusable_space.cpp).

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

See [`examples/layering.cpp`](../examples/layering.cpp).

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
```

`parse` rejects leading/trailing separators and empty segments. Most schema code
can skip `key_path` entirely and pass a string to `anchor::keyspace`.

---

<a id="emit"></a>
## Emitting: templates and documents

`#include "nucleus/config_emitter.h"` (the concept)

Output is the inverse of a source: a sealed space's declared schema can be
projected into a blank document template, and a resolved configuration can be
rendered back out. The format-agnostic contract is the `config_emitter`
concept — a stateless type with two operations, both writing into a caller-owned
`std::ostream`:

```cpp
template<typename Emitter>
concept config_emitter = requires(const Emitter e, const configuration_space &space,
                                  const configuration &config, std::ostream &out) {
    { e.emit_template(space, out) } -> std::same_as<void>;
    { e.emit_document(config, out) } -> std::same_as<void>;
};
```

Each shipped format exposes the pair as free functions in its own namespace,
plus a `struct emitter` modeling the concept:

| Format | Header | Free functions | CMake target |
|--------|--------|----------------|--------------|
| XML  | `"nucleus/xml/xml_emitter.h"`   | `nucleus::xml::emit_template` / `emit_document`  | `nucleus::xml` |
| env  | `"nucleus/env/env_emitter.h"`   | `nucleus::env::emit_template` / `emit_document`  | `nucleus::env` |
| argv | `"nucleus/argv/argv_emitter.h"` | `nucleus::argv::emit_template` / `emit_document` | `nucleus::argv` |

```cpp
nucleus::xml::emit_document(config, std::cout);     // nested XML
nucleus::env::emit_document(config, std::cout);     // KEY=value lines
nucleus::argv::emit_document(config, std::cout);    // --KEY=value flags (delimiter "-", host-selectable)
```

The seam is stream-based and the caller owns persistence — write to a file with
a `std::ofstream`, capture into a string with a `std::ostringstream`. A repeated
path keeps all of its values in every format. See
[`examples/round_trip.cpp`](../examples/round_trip.cpp),
[`examples/xml_persist.cpp`](../examples/xml_persist.cpp), and
[`examples/emit_template.cpp`](../examples/emit_template.cpp).

---

<a id="expected"></a>
## `expected<T, E>` and `error` — fallible returns

`#include "nucleus/expected.h"` and `"nucleus/error.h"`

`expected<T, E>` is the fallible-return vocabulary used across the public API.
It mirrors `std::expected` (C++23); a future migration points the aliases at
the standard type and edits nothing else. Truthy when it holds a value;
construct the error alternative with `nucleus::unexpected(e)`. The type is
`[[nodiscard]]`, so discarding any result — a load, a registration — is a
compiler warning.

Every public result channel carries `nucleus::error` as its `E`:

```cpp
enum class errc {
    unreadable_source, malformed_source, invalid_inheritance, unmet_capability,
    layering_violation, unresolved_token, invalid_selection, schema_violation,
    failed_conversion, rejected_registration, sealed_builder,
    absent_key, missing_converter, mismatched_type,
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
reads. A host branches on `code`; the human detail travels in `message`.
Host-supplied seams (converters, the registration policy verdict) still
traffic in plain reason strings — the engine attaches the code at the seam
where the failure class is known.

```cpp
auto loaded = nucleus::load(space, stack, {});
if(!loaded)
{
    std::cerr << loaded.error() << '\n';   // streams "code: message"
    if(loaded.error().code == nucleus::errc::invalid_selection)
        print_available_strains();
    return 1;
}
const nucleus::configuration &config = loaded.value();
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
`configuration_space_builder::conflicts()` (and by the sealed space, which
carries the ledger forward). Two registrations claiming the same key path
produce one report that names every claimant and refuses to pick a winner;
adjudication is the host's.

```cpp
const std::string &key_path() const;
const std::vector<claimant> &claimants() const;
std::size_t size() const;
std::string describe() const;
```

See [`examples/diagnostics.cpp`](../examples/diagnostics.cpp).

---

<a id="completion"></a>
## Completion: `generate_completion`, `shell`

`#include "nucleus/completion/completion.h"` (for the `shell` enum)

Projects the registered schema into a static shell completion script through the
same flag mapping the CLI surface uses, so completion cannot drift from the CLI.

```cpp
enum class shell { bash, zsh };
std::string configuration_space::generate_completion(shell which, std::string_view prog,
                                                     const cli_delimiter &delimiter = {}) const;
```

A host that re-delimits its CLI (`argv_source::delimit_with`) passes the same
`cli_delimiter` here, keeping the completed flags identical to the parsed ones.
An `enum_element`'s value set becomes that flag's completion candidates. A pure
read of the sealed schema. nucleus is a library, not a CLI — it returns the
script as a string and the host decides how to surface it. See
[`examples/completion.cpp`](../examples/completion.cpp) and
[`examples/argv_delimiter.cpp`](../examples/argv_delimiter.cpp).
