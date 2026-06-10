# Seams you extend

The extension surface: the concepts a host makes a type satisfy, the base
classes it inherits from, and the policy hooks it composes. The core ships
mechanism here; the host supplies policy. For the concrete types nucleus ships
that satisfy these seams, see [Shipped implementations](api-implementations.md).

## Contents

- [`configuration_source` — the source concept](#source_concept)
- [Optional affordances: `projects_source`, `inheriting_source`](#affordances)
- [`source_handle` / `source_stack` — type erasure and composition](#erasure)
- [Batches, values, and the `retained_buffer` lifetime contract](#buffer)
- [`capability_descriptor` — declaring affordances honestly](#capability)
- [`feature_gate` — capability gating](#feature_gate)
- [Inheritance: `inherit_declaration`, `inherit_policy`, `extend_disposition`](#inheritance)
- [Custom tokenizers: `tokenizer_builder` + `install_tokenizer`](#tokenizers)
- [Custom converters: `register_converter`](#converters)
- [`registration_policy` — intercepting registration](#registration_policy)
- [`log_sink` — the logging seam](#log_sink)
- [Discovery: `extension_registry`, `discovery`](#discovery)
- [`path_text`, `cli_surface` — shared transforms](#transforms)
- [Adding a format module: `config_emitter`, `nucleus_add_module`](#format-module)

---

<a id="source_concept"></a>
## `configuration_source` — the source concept

`#include "nucleus/configuration_source/source_concept.h"`

There is no virtual source base class. A source is any plain struct satisfying
a compile-time concept — declare capabilities and produce a batch:

```cpp
template <typename S>
concept configuration_source =
    requires(S s) {
        { s.capabilities() } -> std::convertible_to<capability_descriptor>;
        { s.pull() }         -> std::convertible_to<configuration_source_result>;
    };
```

```cpp
struct table_source
{
    nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting};
    }

    nucleus::configuration_source_result pull()
    {
        nucleus::configuration_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "service/name", nucleus::value::owned("edge"), capabilities()));
        return batch;
    }
};
static_assert(nucleus::configuration_source<table_source>);
```

- **`capabilities()`** — declare which structural affordances this source
  provides (see [`capability_descriptor`](#capability)). Be honest: a source
  that claims an affordance it lacks defeats graceful degradation.
- **`pull()`** — produce one batch of entries, or an error string naming why it
  failed (`configuration_source_result` is
  `expected<configuration_source_batch, std::string>`). The core never silently
  drops a source.

A concept-satisfying struct is moved into a `source_handle` and reaches the
engine through the same fold path as any shipped source. See
[`examples/custom_source.cpp`](../examples/custom_source.cpp) (a custom source
folded by `load`) and [`examples/parser_concept.cpp`](../examples/parser_concept.cpp)
(the concept, the `static_assert`, and pulling through a handle directly).

---

<a id="affordances"></a>
## Optional affordances: `projects_source`, `inheriting_source`

`#include "nucleus/configuration_source/source_concept.h"`

Two further concepts are **detected, never required**. A source opts in simply
by having the member; the type-erased handle dispatches to it when present and
no-ops when absent:

```cpp
// A source that accepts a schema-derived projection before pull().
template <typename S>
concept projects_source =
    configuration_source<S> &&
    requires(S s, const schema_projection & p) { s.apply_projection(p); };

// A source that declares an inheritance chain parent after pull().
template <typename S>
concept inheriting_source =
    configuration_source<S> &&
    requires(const S s) {
        { s.inheritance() } -> std::convertible_to<inherit_declaration>;
    };
```

- `apply_projection(const schema_projection &)` — the load fold hands a
  document source the schema's keyed-container map
  (`"nucleus/schema/projection.h"`: container path → primary-key field name) just
  before `pull()`, so the source can emit one sub-tree per named instance
  instead of collapsing repeated siblings last-wins. Flat sources ignore it.
- `inheritance() const` — called by the chain walker after `pull()` completes;
  returns the source's [`inherit_declaration`](#inheritance).

The shipped `xml_source` models both; `env_source`, `argv_source`, and
`runtime_source` model neither.

---

<a id="erasure"></a>
## `source_handle` / `source_stack` — type erasure and composition

`#include "nucleus/configuration_source/source_handle.h"` and `source_stack.h`

`source_handle` is a move-only value type that erases any concept-satisfying
source behind a small manual vtable; the optional affordances are detected with
`if constexpr` at erasure time, so a source pays only for what it has.

```cpp
template <configuration_source S> explicit source_handle(S s);
capability_descriptor capabilities() const;
void apply_projection(const schema_projection &p);   // no-op if the source does not project
inherit_declaration inheritance() const;             // inherit_default if not inheriting
configuration_source_result pull();
```

`source_stack` composes handles in precedence order (later = higher); the
variadic constructor erases a pack of sources directly, and `push_back` appends
a pre-erased handle. A `load_options::make_document` factory also returns a
`source_handle` — that is how a custom document format reaches the
inheritance chain walker.

---

<a id="buffer"></a>
## Batches, values, and the `retained_buffer` lifetime contract

`#include "nucleus/configuration_source/configuration_source.h"`

```cpp
struct configuration_source_batch {
    std::vector<keyspace_entry> entries;
    std::vector<extend_disposition> dispositions;  // empty for flat sources
    retained_buffer buffer;    // pins any backing memory the entries view into
};
```

`keyspace_entry` (`"nucleus/keyspace/entry.h"`) is
`{ std::string path; value value; capability_descriptor capabilities; }`; build
one with `make_entry(path, value, caps)`.

A `value` (`"nucleus/keyspace/value.h"`) is **view-or-owned**:

```cpp
static value value::view(std::string_view text) noexcept;  // zero-copy; backing must outlive reads
static value value::owned(std::string text);               // self-contained
std::string_view text() const noexcept;
value to_owned() const;
```

### The lifetime contract

If your entries hold **owned** values, attach no buffer — a
default-constructed `retained_buffer` (or `retained_buffer::none()`) pins
nothing. If they hold **views** into a parse arena or byte buffer, the batch
MUST carry ownership of that buffer:

```cpp
retained_buffer::none();                    // owned values, nothing to retain
retained_buffer::owning<MyArena>(args...);  // construct and pin an arena the views point into
explicit retained_buffer(std::shared_ptr<T> held);  // pin an existing shared arena
```

The load copies values out (`value::to_owned`) and only then drops the batch, so
a view never outlives its buffer — but a source that returned views without
pinning the arena would dangle the instant the batch outlived the parser. The
simplest correct source emits owned values and pins nothing
([`examples/custom_source.cpp`](../examples/custom_source.cpp)); the shipped
`xml_source` is the worked arena-pinning example
([Shipped implementations](api-implementations.md#xml_source)).

A document source that permits re-opening a named instance populates
`dispositions`; flat sources leave it empty.

---

<a id="capability"></a>
## `capability_descriptor` — declaring affordances honestly

`#include "nucleus/capability.h"`

A small, trivially comparable bit set of the structural affordances a source
provides. It drives the automatic capability gate every load runs.

```cpp
enum class capability : std::uint8_t {
    nesting,         // a/b/c hierarchy is possible
    duplicate_keys,  // one scope can hold repeated keys
    typed_scalars,   // the source distinguishes int/bool/... from plain text
    comments,        // comments are preserved
    ordering,        // entry order is meaningful
};

constexpr capability_descriptor();                                   // none
constexpr capability_descriptor(std::initializer_list<capability>);  // from a list
constexpr bool supports(capability cap) const noexcept;
constexpr capability_descriptor &with(capability cap) noexcept;      // fluent
```

A descriptor that claims everything is a red flag: it never exercises
degradation. An honest, narrow descriptor (env's is empty) is what proves the
gating mechanism works — and what lets the auto-gate refuse a stack that
genuinely cannot carry the schema's shape
([`examples/capability_gating.cpp`](../examples/capability_gating.cpp)).

---

<a id="feature_gate"></a>
## `feature_gate` — capability gating

`#include "nucleus/configuration_source/feature_gate.h"`

The primitives behind the automatic gate. A requirement is a capability plus a
strength; gating intersects requirements with declared capabilities, applying
loud-vs-quiet:

- a **required** capability no source provides → a loud, named error; gating stops.
- an **optional** capability no source provides → observable degradation: a
  warn-level message through the `log_sink`, recorded and returned.

```cpp
enum class requirement_strength : std::uint8_t { required, optional };
struct feature_requirement { capability cap; requirement_strength strength; };
struct degradation        { capability cap; std::string note; };
struct gated_features     { std::vector<capability> honored; std::vector<degradation> degraded; };
using gate_result = expected<gated_features, gate_error>;   // gate_error = std::string

gate_result gate_features(std::string_view consumer, std::string_view source_name,
                          const capability_descriptor &caps,
                          const std::vector<feature_requirement> &required, log_sink &log);

gate_result gate_stack(std::string_view consumer,
                       const std::vector<std::pair<std::string, capability_descriptor>> &layers,
                       const std::vector<feature_requirement> &required, log_sink &log);
```

`load` runs `gate_stack` automatically: it derives the schema's requirements
from element shape
(`derive_capability_requirements(std::span<const schema_element>)` in
`"nucleus/schema/capability_requirements.h"`) and gates the assembled stack
(whole-stack union — a hard capability is satisfied when ANY layer provides it)
before folding. No host call is required;
`check_capabilities(space, stack, options)` runs the same gate as a standalone
pre-flight. `gate_features` remains the per-source primitive for hosts that
gate a single source directly.

---

<a id="inheritance"></a>
## Inheritance: `inherit_declaration`, `inherit_policy`, `extend_disposition`

`#include "nucleus/configuration_source/inherit_declaration.h"`

These three types form the seam through which a document source declares
ancestry and a host controls chain walking. The core never interprets file
paths or naming conventions.

### `inherit_declaration`

The signal an [`inheriting_source`](#affordances) returns from `inheritance()`
after its `pull()`. The chain walker reads it to decide whether to fetch
another source through `load_options::make_document`.

```cpp
struct inherit_declaration {
    enum class kind { parent_path, inherit_default, opt_out };
    kind which = kind::inherit_default;
    std::string path;  // non-empty only when which == parent_path
};
```

- `parent_path` — this source declares a parent file; the walker fetches it
  through the host's document factory.
- `inherit_default` — no explicit declaration; compose with a base if one
  exists, otherwise no-op. The default when the source returns `{}`.
- `opt_out` — explicitly truncate the chain below this source.

### `inherit_policy`

Passed per load via `load_options::inherit`.

```cpp
struct inherit_policy {
    std::function<std::string(capability_descriptor)> admissibility;
    std::size_t depth_cap = 16;
};
```

- `admissibility` — invoked for each candidate parent source after it is pulled
  (not for the top-level requested source), receiving the candidate's
  `capability_descriptor`. A non-empty return rejects that parent and fails the
  load with the returned string; empty admits. Null (the default) admits all.
- `depth_cap` — the maximum chain depth before the walker fails loudly.

### `extend_disposition`

A re-open declaration carried in `configuration_source_batch::dispositions`. A
derived document that re-opens a named instance from a base document declares
the instance's container path and primary-key value alongside the strength:

```cpp
enum class extend_strength { narrow, wide };

struct extend_disposition {
    std::string container_path;  // e.g. "cluster/server"
    std::string key_value;       // the instance's primary-key value, e.g. "primary"
    extend_strength strength;
};
```

- `narrow` — the re-open obeys the active scope policy.
- `wide` — the re-open composes regardless of scope policy.

A disposition for a primary-key value not present in any base layer is a loud
error. Re-declaring a named instance in a derived layer without `extend` is
rejected as a duplicate — a plain overlay that silently re-opened a named
instance across layers would let an unrelated downstream document mutate an
upstream instance by name collision alone.

The shipped XML grammar for all three (`inherit=` / `extend=` attributes) is in
[Shipped implementations](api-implementations.md#xml_source); the chain test
suite is [`tests/inherit_chain_test.cpp`](../tests/inherit_chain_test.cpp).

---

<a id="tokenizers"></a>
## Custom tokenizers: `tokenizer_builder` + `install_tokenizer`

`#include "nucleus/tokenizer/tokenizer_builder.h"`

The generic core tokenizers (env, string, ...) install automatically on every
builder; a host adds its own `${category....}` vocabulary by building a
`tokenizer` and installing it before `build()`.

```cpp
using token_result = expected<std::string, resolve_error>;
using field_resolver          = std::function<token_result()>;                          // ${cat.name}
using wildcard_field_resolver = std::function<token_result(std::string_view)>;          // ${cat.<any>}
using function_resolver       = std::function<token_result(std::span<const std::string>)>;  // ${cat.name(args...)}

tokenizer_builder(std::string category);
tokenizer_builder &add_field(std::string name, field_resolver resolve);
tokenizer_builder &add_function(std::string name, function_resolver resolve);
tokenizer_builder &set_wildcard(wildcard_field_resolver resolve);
tokenizer build() &&;   // consumes the builder
```

```cpp
nucleus::configuration_space_builder engine;

nucleus::tokenizer_builder builder("greet");
builder.set_wildcard([](std::string_view who) -> nucleus::token_result {
    return std::string("hello ") + std::string(who);
});
engine.install_tokenizer(std::move(builder).build());
nucleus::configuration_space space = engine.build();

// A value "${greet.world}" now resolves to "hello world" at load.
```

Function arguments arrive already expanded (a nested `${...}` inside an arg
resolves first); arity policy lives in the closure. A later installation of the
same category shadows the earlier one. The built-in expansion behavior (fixpoint
recursion, inner-first nesting) is shown in
[`examples/tokens.cpp`](../examples/tokens.cpp); the install path is exercised
in [`tests/resolution_test.cpp`](../tests/resolution_test.cpp).

---

<a id="converters"></a>
## Custom converters: `register_converter`

`#include "nucleus/configuration_space.h"` (the registration) and
`"nucleus/schema/converters.h"` (the element factories)

A converter is `std::function<expected<std::any, std::string>(std::string_view)>`:

- it receives the already-tokenized, fully-expanded text value;
- it returns a `std::any` wrapping exactly a `T` on success — `get_as<T>`
  enforces strict `type_index` equality, no widening, no coercion;
- it returns `unexpected(reason)` on any conversion error and must not throw.

Two ways to attach one:

```cpp
// Per element: the converter travels with the schema_element.
builder.register_element(
    nucleus::typed_element<vec3>("pos", nucleus::anchor::keyspace("body"), make_vec3_converter()));

// Per type: register once, declare elements with registered_element<T>.
builder.register_converter<vec3>(make_vec3_converter());
builder.register_element(
    nucleus::registered_element<vec3>("pos", nucleus::anchor::keyspace("body")));
```

A per-element converter overrides the registry's converter for that element.
Conversion runs after validation on the post-slice keyspace; a failure produces
a load error of the form
`conversion failed for '<path>': <reason> (layer: <label>)` (repeated paths add
the zero-based element index). `make_scalar_converter<T>()` is public so a host
can wrap the built-in parsing with extra validation instead of reimplementing
it — [`examples/typed.cpp`](../examples/typed.cpp) composes the float converter
into an aggregate `vec3` converter.

---

<a id="registration_policy"></a>
## `registration_policy` — intercepting registration

`#include "nucleus/registration_policy.h"`

The seam through which a host pre-validates or refuses a registration before it
commits. The default policy accepts everything — the core imposes no reservation
or namespacing rules of its own.

```cpp
enum class registration_kind : std::uint8_t { schema, tokenizer, configuration_source, converter };
struct registration_request { registration_kind kind; owner_token owner; };

class policy_verdict {
public:
    static policy_verdict accept();
    static policy_verdict reject(std::string reason);
    bool accepted() const noexcept;
    const std::string &reason() const noexcept;
};

class registration_policy {
public:
    virtual ~registration_policy() = default;
    virtual policy_verdict review(const registration_request &request);  // default: accept
};
```

Install it with
`configuration_space_builder::set_registration_policy(std::make_shared<...>())`.
A rejection's `reason()` is surfaced verbatim as the `registration_result`
error. See [`examples/registration_policy.cpp`](../examples/registration_policy.cpp).

---

<a id="log_sink"></a>
## `log_sink` — the logging seam

`#include "nucleus/log_sink.h"`

A minimal level + message contract with a no-op default and zero dependency on
any logging library. The message arrives already formatted.

```cpp
enum class log_level : std::uint8_t { trace, debug, info, warn, error };
constexpr std::string_view to_string(log_level level) noexcept;

class log_sink {
public:
    virtual ~log_sink() = default;
    virtual void log(log_level level, std::string_view message);   // no-op default
};
```

Most hosts do not subclass it — the shipped adapters (`log_sink_f` for a
callable, `log_sink_s` for an `std::ostream`) cover the common cases; see
[Shipped implementations](api-implementations.md#log_sink_adapters) and
[`examples/logging.cpp`](../examples/logging.cpp).

---

<a id="discovery"></a>
## Discovery: `extension_registry`, `discovery`

Mechanism for finding and opening config files. The host supplies *what* (a base
name), *where* (search directories), and *which* (an extension → parser map);
the core does the cross product and reports existing files in the host's
precedence order. Neither type decides filename conventions.

### `extension_registry`

`#include "nucleus/configuration_source/extension_registry.h"`

```cpp
using parser_factory = std::function<source_handle(const std::string &path)>;

extension_result claim(std::initializer_list<std::string_view> extensions,
                       parser_factory factory, owner_token owner = {});  // atomic
std::vector<std::string> extensions() const;
```

A parser may claim several extensions, but each extension resolves to exactly
one parser; a colliding `claim` rejects the whole registration (all-or-nothing)
with a message naming the conflicting owners.

### `discovery`

`#include "nucleus/configuration_source/discovery.h"`

```cpp
struct discovered_source { std::string path; std::string extension; };

static std::vector<discovered_source> discovery::find(
    std::string_view base_name,
    const std::vector<std::filesystem::path> &search_paths,
    const extension_registry &registry);

static std::vector<source_handle> discovery::open_all(
    std::string_view base_name,
    const std::vector<std::filesystem::path> &search_paths,
    const extension_registry &registry);
```

---

<a id="transforms"></a>
## `path_text`, `cli_surface` — shared transforms

Not seams to override but the canonical transforms the engine exposes so a host
stays consistent with it.

### `path_text`

`#include "nucleus/configuration_source/path_text.h"` — the one filesystem-path
→ text conversion, stable across platforms (forward slashes, UTF-8).

```cpp
std::string path_to_text(const std::filesystem::path &p);
```

### `cli_surface`

`#include "nucleus/argv/cli_surface.h"` — the pure, schema-free bijection
between a CLI token and a keyspace assignment. `argv_source` uses it; completion
uses its inverse, which is why the two cannot drift.

```cpp
struct cli_assignment { key_path key; std::string value; };
using cli_normalize_result = expected<cli_assignment, std::string>;

cli_normalize_result normalize_arg(std::string_view raw);   // "--a-b-c=v" -> {a/b/c, v}
```

`-` is always the separator; the split is on the first `=` only; a bare flag
becomes the value `"true"`.

---

<a id="format-module"></a>
## Adding a format module: `config_emitter`, `nucleus_add_module`

A new format (a parser-backed source, an output emitter, or both) ships as its
own per-target module so the core never links it. The shipped `xml` module is
the worked example.

**Layout.** Create `lib/<fmt>/` with a public include root `lib/<fmt>/include`,
and put the public headers under `nucleus/<fmt>/` — every consumer then includes
them as `"nucleus/<fmt>/<fmt>_source.h"` and `"nucleus/<fmt>/<fmt>_emitter.h"`.
A compiled module keeps its implementation (and any third-party parser) under
`lib/<fmt>/src`, reachable only from inside the module.

**Model the input contract.** Make the source a plain struct satisfying
[`configuration_source`](#source_concept) (plus `apply_projection` /
`inheritance` if it is a document format), honoring the
[`retained_buffer`](#buffer) contract for any view-values.

**Model the output contract.** Provide `emit_template(const configuration_space&,
std::ostream&)` and `emit_document(const configuration&, std::ostream&)` as free
functions in a `nucleus::<fmt>` namespace, plus a stateless `struct emitter`
whose members forward to them so the type satisfies
[`config_emitter`](api-using.md#emit). No third-party type appears in the public
header; it lives only in the `.cpp`.

**Register the target** via `nucleus_add_module()` in `lib/CMakeLists.txt`:

```cmake
# Header-only (no parser dependency), like env / argv / runtime:
nucleus_add_module(<fmt> TYPE INTERFACE
    ALIASES nucleus::<fmt> LINK_PUBLIC nucleus::nucleus)

# Compiled (wraps a library, like xml over pugixml -- keep the library PRIVATE):
nucleus_add_module(<fmt> TYPE STATIC
    ALIASES nucleus::<fmt>
    SOURCES <fmt>/src/nucleus/<fmt>/<fmt>_source.cpp <fmt>/src/nucleus/<fmt>/<fmt>_emitter.cpp
    LINK_PUBLIC nucleus::nucleus LINK_PRIVATE <thirdparty>::<thirdparty>)
```

The `INTERFACE`/`PUBLIC` link to `nucleus::nucleus` means a consumer of
`nucleus::<fmt>` also gets core. Core never links a format module, and the
boundary is enforced by the core-purity gate
(`scripts/core_purity_check.{cmake,sh}`).
