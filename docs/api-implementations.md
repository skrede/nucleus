# Shipped implementations

The concrete source and emitter modules nucleus ships, each a worked
realization of the seams in [Seams you extend](api-extending.md) — both for
direct use and as a model for your own. Every source is a plain struct
satisfying the [source concept](api-extending.md#source_concept) by duck
typing; none inherits from anything.

| Module | Source type | Emitter namespace | Header root | CMake target | Kind |
|--------|-------------|-------------------|-------------|--------------|------|
| xml     | `nucleus::xml_source`     | `nucleus::xml`  | `nucleus/xml/`     | `nucleus::xml`     | compiled (pugixml, private) |
| env     | `nucleus::env_source`     | `nucleus::env`  | `nucleus/env/`     | `nucleus::env`     | header-only |
| argv    | `nucleus::argv_source`    | `nucleus::argv` | `nucleus/argv/`    | `nucleus::argv`    | header-only |
| runtime | `nucleus::runtime_source` | —               | `nucleus/runtime/` | `nucleus::runtime` | header-only |

Each module target links `nucleus::nucleus` transitively. The xml module is
built when `NUCLEUS_BUILD_SOURCE_XML=ON` (the default).

## Contents

- [`xml_source` — the document source](#xml_source)
- [`env_source` — a flat source](#env_source)
- [`argv_source` — the CLI-flag source](#argv_source)
- [`runtime_source` — the programmatic source](#runtime_source)
- [The emitters: `config_emitter` realizations](#emitters)
- [`log_sink_f`, `log_sink_s` — log_sink adapters](#log_sink_adapters)

---

<a id="xml_source"></a>
## `xml_source` — the document source

`#include "nucleus/xml/xml_source.h"` · target `nucleus::xml` · satisfies
[`config_source`](api-extending.md#source_concept) plus both optional
affordances (`projects_source`, `inheriting_source`)

The reference document source, backed by pugixml. The xml module is the only
place pugixml is reachable: it is privately linked and nothing of pugixml
appears in the public headers, so the core never sees it.

```cpp
struct xml_source_options {
    enum class input_kind { string, file };
    input_kind  kind = input_kind::string;
    std::string data;  // XML text when kind==string; file path when kind==file

    static xml_source_options of_string(std::string text);
    static xml_source_options of_file(std::string path);
};

class xml_source final {
public:
    static xml_source from(xml_source_options options);

    capability_descriptor capabilities() const;
    config_source_result pull();
    void apply_projection(const schema_projection &projection);
    inherit_declaration inheritance() const;   // callable after pull()
};
```

How it realizes the seams:

- **Tree → keyspace.** Nested elements become `/`-separated key paths; an
  element's attributes and pure-text leaf children (plain character data or
  CDATA sections) become values. The root element name anchors the path. The
  walk caps element nesting at 64 levels; deeper documents are rejected as
  malformed.
- **Errors.** A failed `pull()` distinguishes an unreadable file
  (`errc::unreadable_source`) from a malformed document
  (`errc::malformed_source`), carrying pugixml's parse description — and, for a
  malformed document, the byte offset — in the message.
- **View-node model.** Every value is a view into the pugixml document arena
  (zero-copy), and `pull()` pins that arena in the batch's `retained_buffer` —
  the [buffer lifetime contract](api-extending.md#buffer), honored. This is the
  model to copy for any arena-backed parser.
- **Capabilities:** `{ nesting, duplicate_keys, comments, ordering }`. Not
  `typed_scalars` — XML text is untyped until a schema interprets it.
- **Projection.** `apply_projection` retains the schema's keyed-container map so
  the walk renders one instance per primary-key value instead of collapsing
  repeated siblings last-wins.
- **Inheritance.** It is the only shipped source that participates in
  inheritance chains, via two grammar attributes:
  - `inherit=` on the **document root** populates `inheritance()`:
    `inherit="path/to/parent.xml"` → `kind::parent_path` (the chain walker
    fetches the named file through `load_options::make_document`);
    `inherit="none"` → `kind::opt_out`; absent → `kind::inherit_default`.
    Placing `inherit=` on any non-root element is a loud parse error.
  - `extend=` on a **keyed instance element** emits an `extend_disposition`:
    `extend="narrow"` or `extend="wide"`; any other value is a loud parse
    error.
  The chain walker caps the depth at `inherit_policy::depth_cap` (default 16)
  and consults `inherit_policy::admissibility` for each fetched parent.

A host usually does not construct it at the call site of `load`: it hands a
factory to `load_options::make_document` that builds one per requested path:

```cpp
auto make = [](const std::string &path) -> nucleus::source_handle {
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_file(path)));
};
```

See [`examples/xml.cpp`](../examples/xml.cpp),
[`examples/strains.cpp`](../examples/strains.cpp), and
[`tests/inherit_chain_test.cpp`](../tests/inherit_chain_test.cpp).

---

<a id="env_source"></a>
## `env_source` — a flat source

`#include "nucleus/env/env_source.h"` · target `nucleus::env`

The minimal source: a flat `(path → value)` table the host populates. It is
deliberately honest — its capability descriptor is **empty**, which is what lets
it exercise the gate's degradation and refusal paths (ask a schema with nesting
for an env-only stack and the auto-gate refuses; see
[`examples/capability_gating.cpp`](../examples/capability_gating.cpp)). The
core never reads the process environment; the host decides which variable maps
to which key path.

```cpp
env_source();
explicit env_source(std::vector<std::pair<std::string, std::string>> entries);
env_source &set(std::string path, std::string text);   // fluent

static capability_descriptor descriptor() noexcept;     // empty
capability_descriptor capabilities() const;              // == descriptor()
config_source_result pull();                      // owned values, no retained buffer
```

Because its values are owned, `pull()` pins no buffer. This is the template for
any in-memory source.

```cpp
nucleus::env_source values;
values.set("service/region", "eu-west").set("service/tier", "gold");
```

See [`examples/env.cpp`](../examples/env.cpp).

---

<a id="argv_source"></a>
## `argv_source` — the CLI-flag source

`#include "nucleus/argv/argv_source.h"` · target `nucleus::argv`

Consumes raw argv tokens and maps `--a-b-c=v` onto the key path `a/b/c` via the
[`cli_surface`](api-extending.md#transforms) bijection. The flag delimiter is
`-` by default and host-selectable via a validated `cli_delimiter` (multi-character
delimiters such as `__` are legal). `pull()` runs in two stages: the pure
syntactic mapping, then an optional schema-validation pass driven by a recognizer.

```cpp
enum class unknown_key_policy { strict, lenient };
// key_recognizer = std::function<bool(const key_path &)>  ("nucleus/config_source/argv/key_recognizer.h")

argv_source();
explicit argv_source(std::vector<std::string> args);
argv_source &recognize_with(key_recognizer recognizer);   // which keys are admissible
argv_source &delimit_with(cli_delimiter delimiter);        // flag delimiter, default "-"
argv_source &anchor_at(key_path anchor);                   // fixed path prefix, default none
argv_source &policy(unknown_key_policy policy) noexcept;   // strict (default) | lenient
argv_source &log_to(log_sink &sink) noexcept;

static capability_descriptor descriptor() noexcept;        // { nesting, duplicate_keys }
capability_descriptor capabilities() const;
config_source_result pull();                        // owned values
```

- With no recognizer, every syntactically valid flag is accepted.
- With a recognizer, an unrecognized flag is an **error** under `strict` and is
  **stored with a warn-level message** under `lenient`.
- `nucleus::recognizer_of(space)` derives the recognizer from a sealed space's
  schema surface, which is how argv stays schema-coupled:

```cpp
nucleus::argv_source argv(std::vector<std::string>{"--server-port=8080"});
argv.recognize_with(nucleus::recognizer_of(space));
```

- A custom delimiter changes the whole flag grammar at once; the argv emitter and
  `generate_completion` accept the same `cli_delimiter`, so the projected surface
  and the parsed surface cannot drift:

```cpp
auto delim = nucleus::cli_delimiter::parse("__").value();
nucleus::argv_source argv(std::vector<std::string>{"--server__port=8080"});
argv.delimit_with(delim);
```

- An anchor makes EVERY flag relative to a fixed path prefix, so a keyspace under
  one never-changing root drops it from the whole flag surface (`--host=x` →
  `server/host`). The recognizer still sees the full path, and the emitter and
  `generate_completion` accept the same anchor — keys outside it are not
  addressable in the anchored grammar and are skipped:

```cpp
nucleus::argv_source argv(std::vector<std::string>{"--host=edge"});
argv.anchor_at(nucleus::key_path::parse("server").value());
```

Its capability descriptor is `{ nesting, duplicate_keys }`, on runtime_source's
rationale: the bijection genuinely addresses nested paths, and repeating a flag
(`--tag=a --tag=b`) is the CLI idiom for collections — occurrences compose into
one ordered collection. So a nested schema loads from argv alone. Not
`typed_scalars`: flag values are text, so typing degrades softly. See
[`examples/argv.cpp`](../examples/argv.cpp),
[`examples/argv_recognizer.cpp`](../examples/argv_recognizer.cpp),
[`examples/argv_delimiter.cpp`](../examples/argv_delimiter.cpp), and
[`examples/logging.cpp`](../examples/logging.cpp).

---

<a id="runtime_source"></a>
## `runtime_source` — the programmatic source

`#include "nucleus/runtime/runtime_source.h"` · target `nucleus::runtime`

A first-class in-memory source: a host builds configuration directly via
`.set(path, value)` with no document at all — embedding code, generated config,
tests, defaults layers.

```cpp
runtime_source();
explicit runtime_source(std::vector<std::pair<std::string, std::string>> entries);
runtime_source &set(std::string path, std::string text);   // fluent

capability_descriptor capabilities() const;   // { nesting, duplicate_keys, typed_scalars }
config_source_result pull();            // owned values
```

It emits flat `(path → value)` entries exactly like `env_source`, but declares
`{ nesting, duplicate_keys, typed_scalars }` — a host-built source genuinely
can carry nested, repeated, and typed data, so the auto-gate admits a
nested/typed schema fed programmatically. This is the difference between the
two flat in-memory sources: same fold result, different gate-visible honesty.
The declared descriptor travels on every emitted entry, so the gate's admit
decision and the fold's per-entry checks can never disagree: two `.set()` calls
on a repeated path compose into a collection instead of failing as a
flat-source violation.

```cpp
nucleus::runtime_source defaults;
defaults.set("server/host", "localhost").set("server/port", "8080");
```

See [`examples/source_stack.cpp`](../examples/source_stack.cpp) and
[`examples/reusable_space.cpp`](../examples/reusable_space.cpp).

---

<a id="emitters"></a>
## The emitters: `config_emitter` realizations

Each format module ships the output pair as free functions in its own
namespace, plus a `struct emitter` whose members forward to them so the module
satisfies the [`config_emitter`](api-using.md#emit) concept by type as well as
by call surface. Both operations write into a caller-owned `std::ostream`; the
caller owns persistence. The argv pair takes an optional `cli_delimiter` and
`key_path` anchor (`argv::emitter` carries both as its only state), which must
match the `argv_source` it round-trips with.

| Header | Free functions | Rendering |
|--------|----------------|-----------|
| `"nucleus/xml/xml_emitter.h"`   | `nucleus::xml::emit_template` / `emit_document`   | nested XML; anchor-path nesting; constrained leaves annotated with their allowed set; a repeated path keeps all its values as sibling elements |
| `"nucleus/env/env_emitter.h"`   | `nucleus::env::emit_template` / `emit_document`   | flat `KEY=value` lines, one per resolved value; the template emits one blank `KEY=` per declared leaf with an `# allowed: a\|b\|c` annotation on constrained leaves |
| `"nucleus/argv/argv_emitter.h"` | `nucleus::argv::emit_template` / `emit_document`  | flat `--KEY=value` lines: the key joined by the `cli_delimiter` (default `-`), the exact flags `argv_source` parses back |

`emit_template` projects a **sealed space's declared schema** into a blank
document template; `emit_document` projects a **resolved configuration** into a
populated one. A repeated path emits one entry per value in fold order in every
format.

```cpp
nucleus::xml::emit_template(space, std::cout);    // blank template from the schema
nucleus::xml::emit_document(config, std::cout);   // populated document from a load

std::ofstream file("config.xml");
nucleus::xml::emit_document(config, file);        // persistence is yours
```

The runtime module ships no emitter — its "format" is C++ code. See
[`examples/round_trip.cpp`](../examples/round_trip.cpp) (one configuration
rendered through all three formats),
[`examples/xml_persist.cpp`](../examples/xml_persist.cpp), and
[`examples/emit_template.cpp`](../examples/emit_template.cpp).

---

<a id="log_sink_adapters"></a>
## `log_sink_f`, `log_sink_s` — log_sink adapters

`#include "nucleus/log_sink.h"` · satisfy [`log_sink`](api-extending.md#log_sink)

Two ready-made bridges so a host rarely needs to subclass `log_sink`.

```cpp
template <typename Callable> class log_sink_f final : public log_sink {
public:
    explicit log_sink_f(Callable callable);
    void log(log_level level, std::string_view message) override;  // forwards to the callable
};

class log_sink_s final : public log_sink {
public:
    explicit log_sink_s(std::ostream &stream);
    void log(log_level level, std::string_view message) override;  // writes "[level] message\n"
};
```

- `log_sink_f` wraps any callable invocable as `f(log_level, std::string_view)`
  — typically a lambda. Class template argument deduction makes
  `nucleus::log_sink_f(lambda)` work without naming the type.
- `log_sink_s` wraps an `std::ostream`, prefixing each line with its level.

```cpp
auto sink = nucleus::log_sink_f([](nucleus::log_level lvl, std::string_view msg) {
    std::cout << '[' << nucleus::to_string(lvl) << "] " << msg << '\n';
});
argv.log_to(sink);
```

See [`examples/logging.cpp`](../examples/logging.cpp).
