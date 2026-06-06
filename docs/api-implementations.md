# Shipped implementations

The concrete types nucleus ships that satisfy the seams in
[Seams you extend](api-extending.md). Each is documented here as a worked
realization of a seam -- both for direct use and as a model for your own.

## Contents

- [`env_source` — a flat source](#env_source)
- [`argv_source` — a CLI-flag source](#argv_source)
- [`xml_source` — a document source](#xml_source)
- [`parser_adapter` / `adapt_parser` — concept → source](#parser_adapter)
- [`log_sink_f`, `log_sink_s` — log_sink adapters](#log_sink_adapters)

---

<a id="env_source"></a>
## `env_source` — a flat source

`#include "nucleus/source/env/env_source.h"` · satisfies [`source`](api-extending.md#source)

The minimal source: a flat `(path → value)` table the host populates. It is
deliberately honest -- its capability descriptor is **empty**, which is what lets
it exercise the [feature-gate](api-extending.md#feature_gate) degradation path
(ask it for `nesting` and you get an observable downgrade, not a lie). The core
never reads the process environment; the host decides which variable maps to which
key path.

```cpp
env_source();
explicit env_source(std::vector<std::pair<std::string, std::string>> entries);
env_source &set(std::string path, std::string text);   // fluent

static capability_descriptor descriptor() noexcept;     // empty
capability_descriptor capabilities() const override;    // == descriptor()
source_result pull() override;                           // owned values, retained_buffer::none()
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
## `argv_source` — a CLI-flag source

`#include "nucleus/source/argv/argv_source.h"` · satisfies [`source`](api-extending.md#source)

Consumes raw argv tokens and maps `--a-b-c=v` onto the key path `a/b/c` via the
[`cli_surface`](api-extending.md#transforms) bijection (`-` is always the
separator). `pull()` runs in two stages: the pure syntactic mapping, then an
optional schema-validation pass driven by a host-supplied recognizer.

```cpp
enum class unknown_key_policy { strict, lenient };
using key_recognizer = std::function<bool(const key_path &)>;

argv_source();
explicit argv_source(std::vector<std::string> args);
argv_source &recognize_with(key_recognizer recognizer);  // which keys are admissible
argv_source &policy(unknown_key_policy policy) noexcept;  // strict | lenient
argv_source &log_to(log_sink &sink) noexcept;

static capability_descriptor descriptor() noexcept;       // empty
capability_descriptor capabilities() const override;
source_result pull() override;                            // owned values
```

- With no recognizer, every syntactically valid flag is accepted.
- With a recognizer, an unrecognized flag is an **error** under `strict` and is
  **stored with a warn-level message** under `lenient`.
- `configuration_space::load(args)` constructs one of these for you and wires its
  recognizer to the schema.

See [`examples/argv.cpp`](../examples/argv.cpp) and
[`examples/logging.cpp`](../examples/logging.cpp).

---

<a id="xml_source"></a>
## `xml_source` — a document source

`src/nucleus/xml/xml_source.h` · satisfies [`document_source`](api-extending.md#document_source)

The reference document source, backed by pugixml. It is the **only** place
pugixml is reachable: the module is privately linked and nothing of pugixml
appears in its interface, so the core never sees it. It lives under the internal
`src/` tree, so a consumer that uses it directly adds `src/` to its include path
and links `nucleus::source_xml` (see the `xml` target in
[`examples/CMakeLists.txt`](../examples/CMakeLists.txt)). Built when
`-DNUCLEUS_BUILD_SOURCE_XML=ON` (the default).

```cpp
namespace nucleus::xml {
class xml_source final : public document_source {
public:
    static xml_source from_string(std::string text);   // parse an in-memory string
    static xml_source from_file(std::string path);      // parse a file at pull time

    capability_descriptor capabilities() const override;
    source_result pull() override;
};
}
```

How it realizes the seam:

- **Tree → keyspace.** Nested elements become `/`-separated key paths; an
  element's attributes and pure-text leaf children become values. The root
  element name anchors the path.
- **View-node model.** Every value is a `string_view` into the pugixml pool
  (zero-copy), and `pull()` pins that pool in the batch's `retained_buffer`. The
  resolve copies the values out and only then drops the batch -- the
  [`document_source`](api-extending.md#document_source) contract, honored. This is
  the model to copy for any arena-backed parser.
- **Honest capabilities:** `{ nesting, duplicate_keys, comments, ordering }`. Not
  `typed_scalars` -- XML text is untyped until a schema interprets it.

Usually a host does not name it directly: it hands a `document_factory` to
`configuration_space::load(paths, make)` that builds one per path. See
[`examples/xml.cpp`](../examples/xml.cpp).

---

<a id="parser_adapter"></a>
## `parser_adapter` / `adapt_parser` — concept → source

`#include "nucleus/source/parser_adapter.h"` · bridges [`Parser`](api-extending.md#parser) → [`source`](api-extending.md#source)

Type-erases any type satisfying the `Parser` concept into a runtime-virtual
`source`, owning the parser by value. It is how the compile-time authoring path
reaches the same virtual seam a hand-written subclass uses.

```cpp
template <typename T> class parser_adapter final : public source {
public:
    explicit parser_adapter(T parser);
    capability_descriptor capabilities() const override;  // delegates to the parser
    source_result pull() override;                        // delegates to the parser
};

template <typename T>
std::unique_ptr<source> adapt_parser(T parser);           // factory
```

```cpp
std::unique_ptr<nucleus::source> src = nucleus::adapt_parser(table_parser{});
```

See [`examples/parser_concept.cpp`](../examples/parser_concept.cpp).

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
  -- typically a lambda. Class template argument deduction makes
  `nucleus::log_sink_f(lambda)` work without naming the type.
- `log_sink_s` wraps an `std::ostream`, prefixing each line with its level.

```cpp
auto sink = nucleus::log_sink_f([](nucleus::log_level lvl, std::string_view msg) {
    std::cout << '[' << nucleus::to_string(lvl) << "] " << msg << '\n';
});
argv.log_to(sink);
```

See [`examples/logging.cpp`](../examples/logging.cpp).
