# Types you use

The user-facing vocabulary: the types a host instantiates, passes in, and reads
back. None of these requires subclassing. For the seams a host extends, see
[Seams you extend](api-extending.md).

## Contents

- [`configuration_space` — the facade](#configuration_space)
- [Declaring a schema: `schema_element`, `anchor`, free factories](#schema)
- [Keying model: primary key, uniqueness, strains](#keying)
- [Strain selection: `select()`, `set_strain_scope()`](#selection)
- [`key_path` — addressing the keyspace](#key_path)
- [`configuration` — the resolved result](#configuration)
- [Provenance: `origin`, `provenance`](#provenance)
- [Precedence: `source_stack`, `layer_rank`](#precedence)
- [Built-in sources: `env_source`, `argv_source`](#sources)
- [`result<T, E>` — fallible returns](#result)
- [`owner_token` — opaque identity](#owner_token)
- [Diagnostics: `suggest_keys`, `conflict_report`](#diagnostics)
- [Completion: `generate_completion`, `shell`](#completion)

---

<a id="configuration_space"></a>
## `configuration_space` — the facade

`#include "nucleus/configuration_space.h"`

The entry point. In the configurable phase it owns the schema, tokenizer, and
source registries and accepts registrations; `load()`/`resolve()` transitions it
to the resolved phase and produces a `configuration`. Move-only.

```cpp
configuration_space engine;

// Registration (configurable phase only).
registration_result register_element(schema_element element, owner_token owner = {});
registration_result register_schema(std::string key_path, owner_token owner = {});
registration_result register_tokenizer(std::string name, owner_token owner = {});
registration_result register_source(std::string name, owner_token owner = {});
registration_result install_tokenizer(tokenizer tok, owner_token owner = {});
void set_registration_policy(std::shared_ptr<registration_policy> policy);

// Strain selection and scope (configurable phase only; must be called before load/resolve).
registration_result select(std::string key_value);
registration_result set_strain_scope(strain_scope_policy policy);
registration_result set_inherit_policy(inherit_policy policy);

// Resolve (transitions to the resolved phase).
load_result resolve(const source_stack &stack);
load_result load(const source_stack &stack);                       // alias for resolve
load_result load(std::vector<std::string> args);                   // argv only, schema-wired
load_result load(std::vector<std::string> paths, const document_factory &make);
load_result load(std::vector<std::string> args,
                 std::vector<std::string> paths, const document_factory &make);

// Reads (either phase).
facade_phase phase() const noexcept;
std::size_t schema_count() const noexcept;
std::size_t tokenizer_count() const noexcept;   // includes the auto-installed core tokenizers
std::size_t source_count() const noexcept;
std::vector<conflict_report> conflicts() const;
std::string generate_completion(shell which, std::string_view prog) const;
gate_result gate_capabilities(/* see api-extending.md */) const;
```

- `registration_result` is `result<std::monostate, std::string>`: truthy on
  success, carrying the registration policy's rejection reason on failure.
- `load_result` is `result<configuration, std::string>`.
- `document_factory` is `std::function<std::unique_ptr<source>(const std::string &)>`
  -- the host's "path → source" decision; returning `nullptr` fails the load.
- The single-argument `load(args)` overload wires the argv source's unknown-key
  recognizer to the schema, so an undeclared flag is rejected by the schema
  authority.

Calling `resolve()`/`load()` a second time is an error (`"... already resolved"`).

See [`examples/quickstart.cpp`](../examples/quickstart.cpp).

---

<a id="schema"></a>
## Declaring a schema

`#include "nucleus/schema/schema.h"` and `"nucleus/schema/anchor.h"`

The schema is the authority over what keys may exist. A `schema_element` is one
declared node; free factory functions build the four kinds fluently.

```cpp
struct schema_element {
    std::string name;                       // the leaf segment
    anchor at = anchor::root();             // where it attaches
    bool required = false;                   // must carry a value at resolve
    bool identity = false;                   // this node's selector / primary key
    bool unique = false;                     // value must be distinct across sibling instances
    std::vector<std::string> allowed_values; // closed set; empty = unconstrained
    key_path declared_path() const;          // anchor path + name
    key_path container() const;              // the parent path (the repeatable container)
    bool enforces_uniqueness() const noexcept; // true if identity || unique
};

schema_element element(std::string name, anchor at);
schema_element required_element(std::string name, anchor at);
schema_element identity_element(std::string name, anchor at);
schema_element primary_key_element(std::string name, anchor at);  // alias for identity_element
schema_element unique_element(std::string name, anchor at);
schema_element enum_element(std::string name, anchor at, std::vector<std::string> values);
```

A primary-key field selects one instance from a repeatable container and is
implicitly unique; a `unique_element` constrains values to be distinct across
sibling instances without taking on the selector role. Both are checked at resolve;
violations are loud errors.

### `anchor` — where an element attaches

An anchor is a typed, code-side-only position. It never appears in document text.

```cpp
static anchor anchor::root();                       // introduce a top-level node
static anchor anchor::keyspace(key_path under);     // attach under an existing node
static anchor anchor::keyspace(const std::string &under);  // convenience; parses, collapses to root() if malformed
bool is_root() const noexcept;
const key_path &under() const noexcept;
```

Referential integrity is enforced at attach time: a `keyspace`-anchored element
may only attach under an already-declared node. At resolve, the schema rejects
undeclared keys (with a nearest-key suggestion), missing `required`/`identity`
fields, and values outside an `enum_element`'s `allowed_values`.

```cpp
engine.register_element(nucleus::element("server", nucleus::anchor::root()));
engine.register_element(nucleus::required_element("host", nucleus::anchor::keyspace("server")));
engine.register_element(nucleus::enum_element("mode", nucleus::anchor::keyspace("server"), {"http", "https"}));
```

See [`examples/schema.cpp`](../examples/schema.cpp).

---

<a id="keying"></a>
## Keying model: primary key, uniqueness, strains

Marking an element with `identity` (via `primary_key_element`) makes its parent
container repeatable: multiple instances can coexist in one fileset, each
distinguished by a distinct primary-key value. Exactly one primary-key element is
allowed per configuration space; it is the single slice selector for the whole
schema hierarchy.

`unique_element` constrains sibling values to be distinct without enabling the
selector role. Many unique fields may coexist per container; a primary key is
implicitly unique whether or not `unique` is also set.

Anonymous instances (instances with no primary-key value) are templates: they
compose in document order and are inherited by all named instances. Named
instances compose on top of the template.

Resolution always strips the transient key segment: the resolved keyspace
contains `cluster/server/port`, never `cluster/server/yin/port`. The primary-key
value names which instance was selected, not a permanent path segment.

Duplicate primary-key values within one parse stack are a loud error. Duplicate
`unique` values across sibling instances are a loud error.

```cpp
engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
engine.register_element(nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
engine.register_element(nucleus::unique_element("serial", anchor::keyspace("cluster/server")));
```

---

<a id="selection"></a>
## Strain selection: select(), set_strain_scope()

Select a specific strain before resolve:

```cpp
engine.select("yin");  // keep only the instance whose name == "yin"
```

Rules:

- No selection and exactly one named strain present: auto-resolves to that strain.
- No selection and multiple named strains: loud resolve error listing the available
  strain names.
- Unknown selection key value: loud error.

Scope policy governs which entries survive after the slice step. Set it before
load/resolve:

```cpp
engine.set_strain_scope(nucleus::strain_scope_policy::container_open_until_next_strain);
```

The three values and their effects:

| Policy | Effect |
|--------|--------|
| `file_level` | The entire keyspace is frozen at the strain's defining layer. Every entry whose winning rank exceeds Ld is discarded, keyed and general alike. |
| `space_open_container_closed` | General keyspace entries compose freely from all layers. The strain's keyed entries are frozen at Ld: entries with a winning rank above Ld are excluded. This is the default. |
| `container_open_until_next_strain` | The strain's keyed entries compose from Ld up to but excluding Ls (the first layer that introduces a competing strain). If no competing strain exists above Ld, Ls is unbounded. General entries are unconstrained. |

Both `select()` and `set_strain_scope()` must be called before `load()`/`resolve()`;
calling after resolve is a state-machine error.

See [`examples/strains.cpp`](../examples/strains.cpp).

---

<a id="key_path"></a>
## `key_path` — addressing the keyspace

`#include "nucleus/keyspace/key_path.h"`

A decomposed `/`-separated path. Construct it from text via `parse` (fallible) or
build it segment by segment.

```cpp
static result<key_path, std::string> key_path::parse(std::string_view text);
key_path();
explicit key_path(std::vector<std::string> segments);

bool empty() const;
std::size_t size() const;
const std::vector<std::string> &segments() const;
const std::string &front() const;
const std::string &leaf() const;
key_path parent() const;                 // a/b/c -> a/b
key_path child(std::string segment) const;  // a/b + c -> a/b/c
std::string str() const;                 // canonical "/"-joined form
```

`parse` rejects leading/trailing separators and empty segments. Most schema code
can skip `key_path` entirely and pass a string to `anchor::keyspace`.

---

<a id="configuration"></a>
## `configuration` — the resolved result

`#include "nucleus/entry/configuration.h"`

The immutable, self-owning output of a resolve. All values are owned strings; the
source buffers have already been dropped. Freely readable from many threads.

```cpp
std::optional<std::string> get(const std::string &key) const;
bool contains(const std::string &key) const;
const origin *provenance_of(const std::string &key) const;  // "why is this value X?"
std::size_t size() const;
bool empty() const;
std::vector<std::string> keys() const;  // canonical order
```

---

<a id="provenance"></a>
## Provenance: `origin`, `provenance`

`#include "nucleus/keyspace/provenance.h"`

`provenance_of(key)` returns the `origin` of the value that won, or `nullptr`.

```cpp
struct origin {
    std::size_t rank;     // precedence rank of the winning layer
    std::string layer;    // host-readable label, e.g. "argv"
    owner_token owner;    // opaque token of the winning source
};
```

See [`examples/layering.cpp`](../examples/layering.cpp).

---

<a id="precedence"></a>
## Precedence: `source_stack`, `layer_rank`

`#include "nucleus/entry/precedence.h"`

A `source_stack` is the explicit, ranked set of sources handed to `resolve()`.
Higher ranks win. Sources are borrowed (non-owning) -- they must outlive the
resolve.

```cpp
enum class layer_rank { defaults = 0, env = 1, base = 2, overlay = 3, argv = 4 };

source_stack &add(source &src, std::size_t rank, std::string label, owner_token owner = {});
source_stack &add(source &src, layer_rank rank, std::string label, owner_token owner = {});
const std::vector<source_layer> &layers() const;
bool empty() const;
std::size_t size() const;
```

```cpp
nucleus::source_stack stack;
stack.add(env, nucleus::layer_rank::env, "env");
stack.add(argv, nucleus::layer_rank::argv, "argv");   // argv wins on conflict
```

---

<a id="sources"></a>
## Built-in sources: `env_source`, `argv_source`

Both are concrete sources you instantiate and configure directly. The full
contract -- including how to write your own -- is in
[Shipped implementations](api-implementations.md); the using-side summary:

### `env_source`

`#include "nucleus/source/env/env_source.h"` — a flat `(path → value)` source.
The host chooses which names map to which key paths; the core never reads the
process environment.

```cpp
env_source &set(std::string path, std::string text);   // fluent
```

See [`examples/env.cpp`](../examples/env.cpp).

### `argv_source`

`#include "nucleus/source/argv/argv_source.h"` — maps `--a-b-c=v` onto `a/b/c`
(`-` is always the separator).

```cpp
explicit argv_source(std::vector<std::string> args);
argv_source &recognize_with(key_recognizer recognizer);   // bool(const key_path&)
argv_source &policy(unknown_key_policy policy);            // strict | lenient
argv_source &log_to(log_sink &sink);
```

In `strict` mode an unrecognized flag fails the pull; in `lenient` mode it is
stored as a string and warned through the sink. See
[`examples/argv.cpp`](../examples/argv.cpp).

---

<a id="result"></a>
## `result<T, E>` — fallible returns

`#include "nucleus/result.h"`

The in-house fallible type used across the public API (not `std::expected`, which
is C++23). Truthy when it holds a value.

```cpp
bool has_value() const;
explicit operator bool() const;
T &value();              // and const & / &&
E &error();              // and const & / &&
T value_or(U fallback) const &;

failure<E> fail(E error);   // construct the error alternative
```

```cpp
auto loaded = engine.load(args);
if(!loaded) { std::cerr << loaded.error(); return 1; }
const nucleus::configuration &config = loaded.value();
```

---

<a id="owner_token"></a>
## `owner_token` — opaque identity

`#include "nucleus/identity.h"`

An opaque tag a host attaches to a registration. The core stores and surfaces it
(in conflict reports and provenance) but never interprets it. Default-constructed
tokens are anonymous and each is distinct; a token wrapping a value compares equal
to another wrapping the same type and value.

```cpp
owner_token();              // anonymous, distinct
owner_token(T value);       // typed; equality by wrapped type + value
bool has_value() const;
```

---

<a id="diagnostics"></a>
## Diagnostics: `suggest_keys`, `conflict_report`

### `suggest_keys`

`#include "nucleus/diagnostics/key_suggester.h"` — "did you mean...?" over a set
of known keys, using a class-weighted edit distance.

```cpp
std::vector<std::string> suggest_keys(std::string_view unknown,
                                      std::span<const std::string> known,
                                      std::size_t limit = 3);
```

### `conflict_report`

`#include "nucleus/diagnostics/conflict_report.h"` — returned by
`configuration_space::conflicts()`. Two registrations claiming the same key path
produce one report that names every claimant and refuses to pick a winner;
adjudication is the host's.

```cpp
const std::string &key_path() const;
const std::vector<claimant> &claimants() const;   // claimant { std::string location; owner_token owner; }
std::size_t size() const;
std::string describe() const;                     // human-readable, states "no winner"
```

See [`examples/diagnostics.cpp`](../examples/diagnostics.cpp).

---

<a id="completion"></a>
## Completion: `generate_completion`, `shell`

`#include "nucleus/completion/completion.h"`

Projects the registered schema into a static shell completion script through the
same flag mapping the CLI surface uses, so completion cannot drift from the CLI.

```cpp
enum class shell { bash, zsh };
std::string configuration_space::generate_completion(shell which, std::string_view prog) const;
```

An `enum_element`'s value set becomes that flag's completion candidates. A pure
read of the schema -- callable in either phase. See
[`examples/completion.cpp`](../examples/completion.cpp).
