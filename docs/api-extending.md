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
- [`feature_gate` — capability gating](#feature_gate)
- [`log_sink` — the logging seam](#log_sink)
- [`registration_policy` — intercepting registration](#registration_policy)
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
};
```

A subclass implements two methods:

- **`capabilities()`** — declare which structural affordances this source
  provides (see [`capability_descriptor`](#capability)). Be honest: a source that
  claims an affordance it lacks defeats graceful degradation.
- **`pull()`** — produce one batch of entries, or a `source_error` (a
  `std::string`) naming why it failed. The core never silently drops a source.

### Supporting types

```cpp
using source_error  = std::string;
using source_result = result<source_batch, source_error>;

struct source_batch {
    std::vector<keyspace_entry> entries;
    retained_buffer buffer;     // pins any backing memory the entries view into
};
```

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

It is reachable on the facade as `configuration_space::gate_capabilities(...)`. It
is a host-callable step, not auto-driven by the resolve fold: the current schema
model expresses presence and identity per element but not per-element capability
requirements, so the host supplies the requirements it knows rather than the fold
faking a half-wired integration.

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
