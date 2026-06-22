![Nucleus banner](./docs/banner.svg)
[![Linux](https://github.com/skrede/nucleus/actions/workflows/linux.yml/badge.svg?branch=master)](https://github.com/skrede/nucleus/actions/workflows/linux.yml)
[![macOS](https://github.com/skrede/nucleus/actions/workflows/macos.yml/badge.svg?branch=master)](https://github.com/skrede/nucleus/actions/workflows/macos.yml)
[![Windows](https://github.com/skrede/nucleus/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/skrede/nucleus/actions/workflows/windows.yml)
[![codecov](https://codecov.io/gh/skrede/nucleus/branch/master/graph/badge.svg)](https://codecov.io/gh/skrede/nucleus)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

# nucleus

A program's configuration arrives from many places &mdash; a command line, one or
more documents, the environment &mdash; and nothing keeps them in agreement.

`nucleus` is a domain-neutral C++20 configuration engine that resolves all of
them onto a single hierarchical keyspace (`/`-separated, FQN-style key paths),
with a registered **schema** as the single upstream authority over both the
command-line surface and the document structure at once. It carries zero coupling
to any embedding application and is useful to any C++ program.

Command-line flags (`--a-b-c=v`), XML elements, and environment values all map
onto the *same* keyspace. The schema dictates what keys may exist, so a `--flag`
the schema does not declare is an error and a document key it does not declare is
caught &mdash; the source never decides what is admissible. Sources are pluggable, not
built-in assumptions; ownership, reservation, and filename conventions live in the
host that embeds the engine, never in the core.

## Features

* **Schema as single authority** \
A registered schema (`anchor::root` / `anchor::keyspace`, required, identity, and
closed-value-set elements) dictates BOTH the CLI surface and the document
structure, with referential-integrity enforcement at attach time and value-set
validation at resolve.

* **One keyspace, many sources** \
A concept-based source seam yields keyspace entries `(path -> value)`. argv,
env, a programmatic runtime source, and a separately linked XML module
(wrapping pugixml, privately linked and unreachable from the core) all feed the
same fold; layering precedence is explicit and provenance travels with every
value. Capability gating runs automatically on every load: the schema's shape
derives what it requires, and a source stack that cannot satisfy it fails
loudly before folding.

* **Two-phase lifecycle** \
A `config_space_builder` accepts registrations (`register_*` /
`install_tokenizer`) until `build()` seals it into an immutable
`config_space`. The free function `nucleus::load_config(space, stack, options)`
folds a source stack against the sealed space and yields an immutable, freely
thread-readable `config`. Registration after `build()` is a
state-machine error; the space can be reused across many loads, and the source
stack is borrowed (never consumed), so one stack can pre-flight and load
repeatedly.

* **Token expansion** \
A `${...}` pipeline with generic core tokenizers (env, string) expands values
at load, recursing to a fixpoint with depth and cycle guards. Host-specific
vocabulary (machine identity, logging) is the host's to build with
`tokenizer_builder` and inject through `install_tokenizer()`.

* **Diagnostics and provenance** \
Every failure is a typed `nucleus::error` &mdash; a machine-readable `errc` code a
host branches on, plus a verbatim human-readable message. Nearest-key suggestions
on unknown keys, non-adjudicating conflict reports, and a `provenance_of` answer
to "why is this value X?". Logging is a `log_sink` seam (level + message,
`std::format`, no-op by default) the host bridges to its logger.

* **Schema-projected shell completion** \
`generate_completion` projects the registered schema into a static bash or zsh
completion script &mdash; flag names plus each element's declared value set &mdash; through
the SAME flag mapping the CLI surface uses, so completion cannot drift from the
CLI.

* **Multi-space CLI addressing** \
`multispace_argv_source` partitions one argv token vector across independently-declared
configuration spaces by the flag's first segment (`--alpha-x=1` routes to the `alpha`
space as key `x`). `argv::emit_template`, `argv::emit_document`, and
`generate_completion` all accept an optional `space_name` so the emitted surface
round-trips exactly with the parser. See [`docs/cli-grammar.md`](docs/cli-grammar.md)
for the full grammar reference, setup example, and cross-format envelope table.

* **Repeated containers** \
`repeated` is legal on any schema element &mdash; leaf or container. N sibling
instances each occupy a distinct zero-based ordinal slot in the resolved keyspace
(`cluster/node[0]/port`, `cluster/node[1]/port`); nesting composes
(`node[0]/route[1]/...`). A higher-precedence source layer replaces the
collection wholesale. CLI flags address instances via a plain ordinal segment
(`--cluster-node-0-port=v`). See [`docs/api-using.md`](docs/api-using.md) for the
full addressing rules including `get_all` gather and `errc::index_required`.

* **Configuration walk API** \
`config::root()` returns a value-semantic `config_node` cursor backed by the
immutable `config`. Chainable navigation (`cfg.root()["cluster"]["node"][0]["port"]`)
never fails loudly &mdash; absent keys yield a null-view that propagates. Shape queries
(`kind`, `count`, `children`, `exists`, `path`) and two traversal forms: a
pre-order `visit(fn)` with bool-stop, and an enter/leave `walk(walker)` via
`config_tree_walker`. Repeated instances are visited in numeric ordinal order.

## Build

```sh
cmake -B build -DNUCLEUS_BUILD_TESTS=ON -DNUCLEUS_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

Catch2 and pugixml are fetched automatically via `FetchContent`. `std::format` is
the diagnostic vocabulary; on a toolchain that lacks it, `{fmt}` is fetched as the
fallback. Options: `NUCLEUS_BUILD_TESTS`, `NUCLEUS_BUILD_EXAMPLES`,
`NUCLEUS_BUILD_SOURCE_XML` (on by default), `NUCLEUS_BUILD_SANITIZER`,
`NUCLEUS_COVERAGE`.

## Consuming nucleus

nucleus installs a standard CMake package. Build and install, then
`find_package`:

```sh
cmake -B build
cmake --build build
cmake --install build --prefix /your/prefix
```

```cmake
find_package(nucleus 0.2 REQUIRED)
target_link_libraries(app PRIVATE nucleus::nucleus nucleus::xml)
```

Or vendor it directly with `FetchContent` (or `add_subdirectory`), which defines
the same targets:

```cmake
include(FetchContent)
FetchContent_Declare(nucleus
    GIT_REPOSITORY https://github.com/skrede/nucleus.git
    GIT_TAG v0.2.0)
FetchContent_MakeAvailable(nucleus)
```

The exported targets are `nucleus::nucleus` (the core engine) and one per source
module: `nucleus::xml` (compiled, built with `NUCLEUS_BUILD_SOURCE_XML=ON`, the
default), and the header-only `nucleus::env`, `nucleus::argv`, and
`nucleus::runtime`. Each module target links `nucleus::nucleus` transitively;
link only the modules you use.

## Documentation

The [`docs/`](docs/) directory holds the API reference, split three ways:
[types you use](docs/api-using.md), [seams you extend](docs/api-extending.md),
and the [shipped implementations](docs/api-implementations.md) of those seams.
Contributions follow [`CONVENTIONS.md`](CONVENTIONS.md).

## Examples

See [`examples/`](examples/) for a small, self-contained program per concept --
quickstart, schema, argv, env, layering, source stacks, custom sources, the
source concept, strains, typed fields, tokens, logging, completion, diagnostics,
the registration policy, capability gating, XML, and the emitters (template,
persist, round trip). Build them with `-DNUCLEUS_BUILD_EXAMPLES=ON`.

### Quickstart

Declare a schema on a builder, seal it, and resolve a command line against it.
The schema is the authority: it decides which flags exist, so `--server-port` is
declared and resolves, while an undeclared flag would fail the load.

```cpp
nucleus::config_space_builder builder;
if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
    return 1;
if(!builder.register_element(
    nucleus::element("port", nucleus::anchor::keyspace("server"))))
    return 1;
nucleus::config_space space = builder.build();

nucleus::argv_source argv(std::vector<std::string>{"--server-port=8080"});
argv.recognize_with(nucleus::recognizer_of(space));

auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(argv)}, {});
if(!loaded)
{
    std::cerr << "load failed: " << loaded.error() << '\n';
    return 1;
}

const nucleus::config &config = loaded.value();
std::cout << "server/port = " << config.get("server/port").value() << '\n';
```

```txt
server/port = 8080
```

### Tokens

Values carrying `${...}` expressions are expanded at load by the core tokenizers,
which every `config_space` installs automatically. A token nested inside
another resolves inner-first. Tokenizer-function arguments are **named and typed**
(`${string.upper(value=...)}`); see
[Named tokenizer arguments](docs/named-tokenizer-arguments.md).

```cpp
nucleus::env_source values;
values.set("service/region", "${string.upper(value=${env.NUCLEUS_REGION})}")
      .set("service/instance", "${string.lower(value=NODE-${env.NUCLEUS_REGION})}");
```

```txt
service/instance = node-eu-west
service/region = EU-WEST
```

### Logging

Inject a `log_sink` to redirect the engine's diagnostics. The default sink is a
no-op; a host bridges the seam to its own logger by wrapping a callable
(`log_sink_f`), wrapping an `ostream` (`log_sink_s`), or subclassing `log_sink`.

```cpp
auto sink = nucleus::log_sink_f(
    [&warnings](nucleus::log_level level, std::string_view message) {
        ++warnings;
        std::cout << "[app/" << nucleus::to_string(level) << "] " << message << '\n';
    });

nucleus::argv_source args(
    std::vector<std::string>{"--service-name=edge", "--service-mode=fast"});
args.recognize_with([](const nucleus::key_path &path) {
        return path.str() == "service/name";
    })
    .policy(nucleus::unknown_key_policy::lenient)
    .log_to(sink);
```

```txt
[app/warn] unknown CLI flag '--service-mode=fast'; lenient mode -- stored as string at 'service/mode'
routed 1 warning(s) through the sink
```

### Completion

Project a registered schema into a shell completion script. `nucleus` is a
library, not a CLI, so it ships no `completion` subcommand &mdash; it returns the
script as a string and the host decides how to surface it.

```cpp
nucleus::config_space_builder builder;
if(!builder.register_element(nucleus::element("logging", nucleus::anchor::root())))
    return 1;
if(!builder.register_element(nucleus::enum_element(
    "level", nucleus::anchor::keyspace("logging"),
    {"debug", "info", "warn", "error"})))
    return 1;
nucleus::config_space space = builder.build();

std::cout << space.generate_completion(nucleus::shell::bash, "mytool");
```

```bash
_mytool_complete()
{
    ...
    case "$flag" in
    '--logging-level')
        COMPREPLY=( $(compgen -W 'debug info warn error' -- "$val") )
        ...
    esac
    ...
}
complete -F _mytool_complete mytool
```

The enum element's declared value set (`debug info warn error`) becomes that
flag's completion candidates, so the offered values can never disagree with the
values the schema validates.

## Limitations

The scope is still deliberately narrow.

* **Sources**: XML documents plus argv, env, and the programmatic runtime
  source. Other document formats (YAML, INI, JSON) are not yet implemented
  &mdash; the source concept is the extension point, but only the XML document
  module ships.
* **Completion** is static, for **bash and zsh** only. There is no dynamic
  (runtime) completion and no fish support; both are future single-file additions
  through the same emission seam.
* **No mutation or merge** of a resolved `config`. The resolved value is
  read-only (it can be emitted back out as XML, env, or argv text); producing a
  new configuration as a value (clone, transfer, diff) is future work.
* **Document inheritance** (`inherit=` chains) is implemented by the XML module
  only; a custom document source must implement the inheritance affordance
  itself to participate.
* **Cross-platform** support (macOS, Linux, Windows; `std::format` with a `{fmt}`
  fallback) is verified by the CI matrix.

The public API may still change while the engine stabilizes.

## License

Apache-2.0. See [`LICENSE`](LICENSE).
