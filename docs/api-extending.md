# Seams you extend

The extension surface: the base classes a host inherits from, the concepts it
makes a type adhere to, and the policy interfaces it composes. The core ships
mechanism here; the host supplies policy. For the concrete types nucleus ships
that satisfy these seams, see [Shipped implementations](api-implementations.md).

## Contents

- [`source` — the provider seam](#source)
- [`capability_descriptor` — declaring affordances](#capability)
- [`document_source` — the document subcategory](#document_source)
- [`Parser` — the compile-time parser concept](#parser)
- [Typed converter: `schema_element::converter`](#typed-converter)
- [`feature_gate` — capability gating](#feature_gate)
- [`log_sink` — the logging seam](#log_sink)
- [`registration_policy` — intercepting registration](#registration_policy)
- [Inheritance: `inherit_declaration`, `inherit_policy`, `extend_disposition`](#inheritance)
- [Discovery: `extension_registry`, `discovery`](#discovery)
- [`path_text`, `cli_surface` — shared transforms](#transforms)

---

<a id="source"></a>
## `source` — the provider seam

`#include "nucleus/source/source.h"`

The single boundary every source crosses. A source yields keyspace entries
`(path → value + capability flags)` and declares its affordances. argv, env, and
documents are all subcategories of this one interface.

```cpp
class source {
public:
    virtual ~source() = default;
    [[nodiscard]] virtual capability_descriptor capabilities() const = 0;
    [[nodiscard]] virtual source_result pull() = 0;

    // Called by the resolve fold just before pull(), passing a schema-derived
    // projection. A document source uses it to project repeatable keyed
    // containers faithfully (one instance per primary-key value) instead of
    // collapsing repeated siblings last-wins. Default: no-op. Flat sources
    // (env, argv) and sources that do not opt in ignore it.
    virtual void apply_projection(const schema_projection &) {}

    // Called after pull() to declare this source's inheritance chain parent,
    // if any. Default: no-op (returns inherit_default). Flat sources ignore it.
    [[nodiscard]] virtual inherit_declaration inheritance() const { return {}; }
};
```

A subclass implements two required methods and may override the two optional ones:

- **`capabilities()`** — declare which structural affordances this source
  provides (see [`capability_descriptor`](#capability)). Be honest: a source that
  claims an affordance it lacks defeats graceful degradation.
- **`pull()`** — produce one batch of entries, or a `source_error` (a
  `std::string`) naming why it failed. The core never silently drops a source.
- **`apply_projection()`** -- override when the source is a document that needs
  the schema's keyed-container map to emit one sub-tree per named instance.
- **`inheritance()`** -- override when the source can declare a parent file; the
  chain walker calls it after `pull()` completes.

### Supporting types

```cpp
using source_error  = std::string;
using source_result = result<source_batch, source_error>;

struct source_batch {
    std::vector<keyspace_entry> entries;
    std::vector<extend_disposition> dispositions;  // empty for flat sources
    retained_buffer buffer;     // pins any backing memory the entries view into
};
```

A document source that permits re-opening a named instance populates
`dispositions`; flat sources leave it empty.

`keyspace_entry` (`"nucleus/keyspace/entry.h"`) is `{ std::string path;
value value; capability_descriptor capabilities; }`; build one with
`make_entry(path, value, caps)`.

A `value` (`"nucleus/keyspace/value.h"`) is **view-or-owned**:

```cpp
static value value::view(std::string_view text);   // zero-copy; backing must outlive reads
static value value::owned(std::string text);        // self-contained
std::string_view text() const;
value to_owned() const;
```

### Buffer lifetime

If your entries hold **owned** values, return `retained_buffer::none()` -- there
is nothing to pin. If they hold **views** into a parse arena or byte buffer, the
batch must carry ownership of that buffer in `retained_buffer`:

```cpp
retained_buffer::none();                    // owned values, nothing to retain
retained_buffer::owning<MyArena>(args...);  // pin an arena the views point into
```

The resolve copies values out (`to_owned`) and only then drops the batch, so a
view never outlives its buffer. The simplest correct source emits owned values
and pins nothing -- see [`examples/custom_source.cpp`](../examples/custom_source.cpp).

---

<a id="capability"></a>
## `capability_descriptor` — declaring affordances

`#include "nucleus/capability.h"`

A small, trivially comparable bit set of the structural affordances a source
provides. It drives feature gating.

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

```cpp
capability_descriptor capabilities() const override {
    return {capability::nesting, capability::ordering};
}
```

A descriptor that claims everything is a red flag: it never exercises
degradation. An honest, narrow descriptor (env's is empty) is what proves the
gating mechanism works.

---

<a id="document_source"></a>
## `document_source` — the document subcategory

`#include "nucleus/source/document_source.h"`

An intermediate base for sources that parse structured input into a tree and walk
it into entries whose values are **views into the parser's retained arena**. It
adds no new methods -- `capabilities()` and `pull()` are still pure -- but it
names one load-bearing contract in a single auditable place:

> Every batch a `document_source` returns must carry, in its `retained_buffer`,
> ownership of the arena its view-values point into.

Inherit from this (rather than `source` directly) when your source is a document
parser holding an arena. The shipped XML source does -- see
[Shipped implementations](api-implementations.md#xml_source).

---

<a id="parser"></a>
## `Parser` — the compile-time parser concept

`#include "nucleus/source/parser.h"`

The format-neutral authoring path that needs no inheritance. Any type with the
two members satisfies the concept and can be type-erased into a `source` via
[`adapt_parser`](api-implementations.md#parser_adapter).

```cpp
template <typename T>
concept Parser = requires(T parser) {
    { parser.capabilities() } -> std::convertible_to<capability_descriptor>;
    { parser.pull() }         -> std::convertible_to<source_result>;
};
```

```cpp
struct table_parser {
    capability_descriptor capabilities() const { return {capability::ordering}; }
    source_result pull() const { /* ... */ }
};
static_assert(nucleus::Parser<table_parser>);
```

Two authoring paths, one runtime seam: a hand-written `source` subclass and a
concept-satisfying struct both reach the engine as `source&`. See
[`examples/parser_concept.cpp`](../examples/parser_concept.cpp).

---

<a id="typed-converter"></a>
## Typed converter: `schema_element::converter`

`#include "nucleus/schema/converters.h"` (for `typed_element<T>` and `make_scalar_converter<T>`)
`#include "nucleus/schema/schema.h"` (for `schema_element`)

A `schema_element` carries two optional typed-seam fields:

```cpp
struct schema_element {
    // ... other fields ...
    std::function<result<std::any, std::string>(std::string_view)> converter;
    std::optional<std::type_index> type_identity;
};
```

`converter` is a bare `std::function` -- an empty (default-constructed) function means no
conversion. Both fields are set together by `typed_element<T>()`; a plain `element()` leaves
them empty. A path with a converter but no type identity (or vice versa) is never consulted
by the resolve pipeline -- both must be present for conversion to run.

### Position in the resolve pipeline

Conversion runs after `validate()` and before `freeze()`, on the post-slice keyspace. Every
path whose schema element carries a converter is visited; absent paths are silently skipped.
Only paths that survived slicing and scope-policy filtering are converted -- a bad value in a
pruned strain or an excluded layer is never reached.

A conversion failure produces a resolve error in the form:

```
conversion failed for 'path': <converter reason> (layer: <winning layer label>)
```

For repeated paths, the per-element form names the zero-based index:

```
conversion failed for 'path' element [N]: <converter reason> (layer: <winning layer label>)
```

### Converter contract

A converter is `std::function<result<std::any, std::string>(std::string_view)>`:

- Receives the already-tokenized, fully-resolved text value.
- Returns `std::any` wrapping the converted value of type `T` on success.
- Returns `fail(reason)` on any conversion error -- must not throw.
- The `std::any` must carry exactly `typeid(T)` (the same type passed to
  `typed_element<T>`); the access-time `get_as<T>` check is a strict `type_index`
  equality test -- no widening, no coercion.

The built-in `make_scalar_converter<T>()` is exposed so a host can compose it: wrap it
with extra validation without reimplementing the low-level parsing.

---

<a id="feature_gate"></a>
## `feature_gate` — capability gating

`#include "nucleus/source/feature_gate.h"`

Computes feature availability as the intersection of a consumer's requirements
with a source's capabilities, applying loud-vs-quiet:

- a **required** capability the source lacks → a loud, named error (both consumer
  and source named); gating stops.
- an **optional** capability the source lacks → observable degradation: a
  warn-level message through the `log_sink`, recorded and returned.

```cpp
enum class requirement_strength { required, optional };
struct feature_requirement { capability cap; requirement_strength strength; };
struct degradation        { capability cap; std::string note; };
struct gated_features     { std::vector<capability> honored; std::vector<degradation> degraded; };
using gate_result = result<gated_features, gate_error>;

gate_result gate_features(std::string_view consumer, std::string_view source_name,
                          const capability_descriptor &caps,
                          const std::vector<feature_requirement> &required,
                          log_sink &log);
```

`load_configuration` runs this gate automatically: it derives the schema's
capability requirements from element shape and gates the assembled source stack
(whole-stack union -- a hard capability is satisfied when ANY layer provides it)
before folding, so a hard shortfall is a loud named error and a soft one degrades
observably. No host call is required. `check_capabilities(const configuration_space &,
const source_stack_options &)` runs the same gate as a standalone pre-flight, without
folding or pulling for resolution, so a host can validate fit ahead of a load; the
pre-flight and the load never disagree. `gate_features` above remains the per-source
primitive for hosts that gate a single source directly.

---

<a id="log_sink"></a>
## `log_sink` — the logging seam

`#include "nucleus/log_sink.h"`

A minimal level + message contract with a no-op default and zero dependency on
any logging library. The message arrives already formatted. Override `log` to
bridge to a real logger.

```cpp
enum class log_level { trace, debug, info, warn, error };
constexpr std::string_view to_string(log_level level);

class log_sink {
public:
    virtual ~log_sink() = default;
    virtual void log(log_level level, std::string_view message);   // no-op default
};
```

Most hosts do not subclass it -- the shipped adapters (`log_sink_f` for a
callable, `log_sink_s` for an `std::ostream`) cover the common cases; see
[Shipped implementations](api-implementations.md#log_sink_adapters) and
[`examples/logging.cpp`](../examples/logging.cpp).

---

<a id="registration_policy"></a>
## `registration_policy` — intercepting registration

`#include "nucleus/registration_policy.h"`

The seam through which a host pre-validates or refuses a registration before it
commits. The default policy accepts everything -- the core imposes no reservation
or namespacing rules of its own.

```cpp
enum class registration_kind { schema, tokenizer, source };
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

Install it with `configuration_space::set_registration_policy(std::make_shared<...>())`.
A rejection's `reason()` is surfaced verbatim as the `registration_result` error.
See [`examples/registration_policy.cpp`](../examples/registration_policy.cpp).

---

<a id="inheritance"></a>
## Inheritance: inherit_declaration, inherit_policy, extend_disposition

`#include "nucleus/source/inherit_declaration.h"`

These three types together form the seam through which a document source declares
ancestry and a host controls chain walking. All three are in the same header; the
core never interprets file paths or naming conventions.

### inherit_declaration

The signal a source returns from `inheritance()` after its `pull()`. The chain
walker reads it to decide whether to fetch another source.

```cpp
struct inherit_declaration {
    enum class kind { parent_path, inherit_default, opt_out };
    kind which = kind::inherit_default;
    std::string path;  // non-empty only when which == parent_path
};
```

The three kinds:

- `parent_path` -- this source declares a parent file; the chain walker fetches it
  using the host's document factory.
- `inherit_default` -- no explicit declaration; compose with a base if one exists,
  otherwise no-op. This is the default when the source returns `{}`.
- `opt_out` -- explicitly truncate the chain below this source. No parent is
  fetched regardless of what a base layer might declare.

### inherit_policy

The host-injectable policy installed via
`configuration_space::set_inherit_policy(inherit_policy)`. Must be called before
`load()`/`resolve()`.

```cpp
struct inherit_policy {
    std::function<std::string(const source &)> admissibility;
    std::size_t depth_cap = 16;
};
```

- `admissibility` -- invoked for each candidate parent source after it is pulled
  (not for the top-level requested source). A non-empty return value rejects that
  parent and fails the load with the returned string as the error. An empty return
  admits the source. A null admissibility callback (the default) admits all sources.
- `depth_cap` -- the maximum inheritance chain depth before the walker fails with
  a loud error. Default is 16.

### extend_disposition

A re-open declaration carried in `source_batch::dispositions`. A derived document
that re-opens a named instance from a base document declares the instance's
container path and primary-key value alongside the extend strength.

```cpp
enum class extend_strength { narrow, wide };

struct extend_disposition {
    std::string container_path;  // e.g. "cluster/server"
    std::string key_value;       // the instance's primary-key value, e.g. "primary"
    extend_strength strength;
};
```

The two strengths:

- `narrow` -- the re-open is honored where the active scope policy admits
  container changes. Subject to `set_strain_scope()` filtering.
- `wide` -- the re-open composes regardless of scope policy. An explicit
  site-level override: the derived layer's entries for this instance are always
  admitted.

A disposition for a primary-key value not present in any base layer is a loud
error (extend-without-base is rejected at resolve).

### XML grammar -- shipped implementation (xml_source)

The XML source (`nucleus::xml::xml_source`) translates document attributes into
`inherit_declaration` and `extend_disposition` values automatically.

**Root element:** the `inherit=` attribute populates `inherit_declaration`:

- `inherit="path/to/parent.xml"` -- sets `kind::parent_path`; the chain walker
  fetches the named file through the host's document factory.
- `inherit="none"` -- sets `kind::opt_out`; the chain is truncated at this source.
- Attribute absent -- sets `kind::inherit_default`.

The `inherit=` attribute is valid only on the document root; placing it on any
other element is a loud parse error naming the offending element.

**Instance elements:** the `extend=` attribute declares a re-open disposition:

- `extend="narrow"` -- emits an `extend_disposition` with `strength::narrow`.
- `extend="wide"` -- emits an `extend_disposition` with `strength::wide`.
- Any other value is a loud parse error.

See [`examples/strains.cpp`](../examples/strains.cpp) for a complete example and
[`tests/inherit_chain_test.cpp`](../tests/inherit_chain_test.cpp) for the full
inheritance chain test suite.

---

<a id="discovery"></a>
## Discovery: `extension_registry`, `discovery`

Mechanism for finding and opening config files. The host supplies *what* (a base
name), *where* (search directories), and *which* (an extension → parser map); the
core does the cross product and reports existing files in the host's precedence
order. Neither type decides filename conventions.

### `extension_registry`

`#include "nucleus/source/extension_registry.h"`

```cpp
using parser_factory = std::function<std::unique_ptr<source>(const std::string &path)>;

extension_result claim(std::initializer_list<std::string_view> extensions,
                       parser_factory factory, owner_token owner = {});  // atomic
bool claims(std::string_view extension) const;
std::unique_ptr<source> open(const std::string &path) const;            // nullptr if unclaimed
std::vector<std::string> extensions() const;
static std::string normalize(std::string_view extension);
static std::string extension_of(std::string_view path);
```

A parser may claim several extensions, but each extension resolves to exactly one
parser; a colliding `claim` rejects the whole registration (all-or-nothing).

### `discovery`

`#include "nucleus/source/discovery.h"`

```cpp
struct discovered_source { std::string path; std::string extension; };

static std::vector<discovered_source> discovery::find(
    std::string_view base_name,
    const std::vector<std::filesystem::path> &search_paths,
    const extension_registry &registry);

static std::vector<std::unique_ptr<source>> discovery::open_all(
    std::string_view base_name,
    const std::vector<std::filesystem::path> &search_paths,
    const extension_registry &registry);
```

---

<a id="transforms"></a>
## `path_text`, `cli_surface` — shared transforms

These are not seams to override but the canonical transforms the engine exposes so
a host stays consistent with it.

### `path_text`

`#include "nucleus/source/path_text.h"` — the one filesystem-path → text
conversion, stable across platforms (forward slashes, UTF-8).

```cpp
std::string path_to_text(const std::filesystem::path &p);
```

### `cli_surface`

`#include "nucleus/source/argv/cli_surface.h"` — the pure, schema-free bijection
between a CLI token and a keyspace assignment. `argv_source` uses it; completion
uses its inverse, which is why the two cannot drift.

```cpp
struct cli_assignment { key_path key; std::string value; };
using cli_normalize_result = result<cli_assignment, std::string>;

cli_normalize_result normalize_arg(std::string_view raw);   // "--a-b-c=v" -> {a/b/c, v}
std::string flag_of(const key_path &path);                  // inverse: a/b/c -> "--a-b-c"
```

`-` is always the separator; the split is on the first `=` only; a bare flag
becomes the value `"true"`.
