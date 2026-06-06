[![Build](https://github.com/skrede/nucleus/actions/workflows/ci.yml/badge.svg)](https://github.com/skrede/nucleus/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/skrede/nucleus/branch/main/graph/badge.svg)](https://codecov.io/gh/skrede/nucleus)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

# nucleus

A program's configuration arrives from many places -- a command line, one or
more documents, the environment -- and nothing keeps them in agreement.

`nucleus` is a domain-neutral C++20 configuration engine that resolves all of
them onto a single hierarchical keyspace (`/`-separated, FQN-style key paths),
with a registered **schema** as the single upstream authority over both the
command-line surface and the document structure at once. It carries zero coupling
to any embedding application and is useful to any C++ program.

Command-line flags (`--a-b-c=v`), XML elements, and environment values all map
onto the *same* keyspace. The schema dictates what keys may exist, so a `--flag`
the schema does not declare is an error and a document key it does not declare is
caught -- the source never decides what is admissible. Sources are pluggable, not
built-in assumptions; ownership, reservation, and filename conventions live in the
host that embeds the engine, never in the core.

## Features

* **Schema as single authority** \
A registered schema (`anchor::root` / `anchor::keyspace`, required, identity, and
closed-value-set elements) dictates BOTH the CLI surface and the document
structure, with referential-integrity enforcement at attach time and value-set
validation at resolve.

* **One keyspace, many sources** \
The `source` / `provider` seam yields keyspace entries `(path -> value)`. argv,
env, and a separately linked XML module (wrapping pugixml, privately linked and
unreachable from the core) all feed the same fold; layering precedence is
explicit and provenance travels with every value.

* **Two-phase lifecycle** \
A `configuration_space` is configurable (`register_*` / `install_tokenizer`)
until `load()` / `resolve()`, which yields an immutable, freely thread-readable
`configuration`. Registration after resolve is a state-machine error.

* **Token expansion** \
A `${...}` pipeline with generic core tokenizers (env, uuid, file/dir/self,
string, scope) expands values at load, recursing to a fixpoint with depth and
cycle guards. An opt-in `HOST` module adds machine-identity tokens.

* **Diagnostics and provenance** \
Nearest-key suggestions on unknown keys, non-adjudicating conflict reports, and a
`provenance_of` answer to "why is this value X?". Logging is a `log_sink` seam
(level + message, `std::format`, no-op by default) the host bridges to its logger.

* **Schema-projected shell completion** \
`generate_completion` projects the registered schema into a static bash or zsh
completion script -- flag names plus each element's declared value set -- through
the SAME flag mapping the CLI surface uses, so completion cannot drift from the
CLI.

## Build

```sh
cmake -B build -DNUCLEUS_BUILD_TESTS=ON -DNUCLEUS_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

Catch2 and pugixml are fetched automatically via `FetchContent`. `std::format` is
the diagnostic vocabulary; on a toolchain that lacks it, `{fmt}` is fetched as the
fallback. Options: `NUCLEUS_BUILD_TESTS`, `NUCLEUS_BUILD_EXAMPLES`,
`NUCLEUS_BUILD_SOURCE_XML` (on by default), `NUCLEUS_BUILD_TOKENIZER_HOST`,
`NUCLEUS_BUILD_SANITIZER`, `NUCLEUS_COVERAGE`.

## Documentation

The [`docs/`](docs/) directory holds the API reference, split three ways:
[types you use](docs/api-using.md), [seams you extend](docs/api-extending.md),
and the [shipped implementations](docs/api-implementations.md) of those seams.

## Examples

See [`examples/`](examples/) for a small, self-contained program per concept --
quickstart, schema, argv, env, layering, custom sources, the parser concept,
tokens, logging, completion, diagnostics, the registration policy, and XML. Build
them with `-DNUCLEUS_BUILD_EXAMPLES=ON`.

### Quickstart

Register a small schema, then resolve a document and a command line onto the same
keyspace. The command line outranks the document, so it overrides `mode`, while
the document's `host` survives because no flag contests it -- and provenance
records which layer won each key.

```cpp
nucleus::configuration_space engine;
engine.register_element(nucleus::element("server", nucleus::anchor::root()));
engine.register_element(nucleus::required_element(
    "host", nucleus::anchor::keyspace("server")));
engine.register_element(nucleus::enum_element(
    "mode", nucleus::anchor::keyspace("server"), {"http", "https"}));

const char *document = R"(<server host="127.0.0.1" mode="http"/>)";
auto make = [document](const std::string &) -> std::unique_ptr<nucleus::source> {
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from_string(document));
};

auto config = engine.load({"--server-mode=https"}, {"config.xml"}, make).value();
```

```txt
resolved 2 key(s):
  server/host = 127.0.0.1  (from path:config.xml)
  server/mode = https  (from argv)
contains server/host: true
```

### Tokens

Values carrying `${...}` expressions are expanded at load by the core tokenizers,
which every `configuration_space` installs automatically. A token nested inside
another resolves inner-first.

```cpp
nucleus::env_source values;
values.set("service/region", "${string.upper(${env.NUCLEUS_REGION})}")
      .set("service/banner", "${string.concat(node-, ${env.NUCLEUS_REGION})}")
      .set("service/instance", "${uuid.v4()}");
```

```txt
service/banner = node-eu-west
service/instance = 100d2b59-9f45-4ac1-9011-417632112711
service/region = EU-WEST
```

### Logging

Inject a custom `log_sink` to redirect the engine's diagnostics. The default sink
is a no-op; a host bridges the seam to its own logger by subclassing it, or by
wrapping a callable (`log_sink_f`) or an `ostream` (`log_sink_s`).

```cpp
class counting_sink final : public nucleus::log_sink
{
public:
    void log(nucleus::log_level level, std::string_view message) override
    {
        std::cout << "[app/" << nucleus::to_string(level) << "] " << message << '\n';
    }
};
```

```txt
[app/warn] unknown CLI flag '--service-mode=fast'; lenient mode -- stored as string at 'service/mode'
the engine routed 1 message(s) through the injected sink
```

### Completion

Project a registered schema into a shell completion script. `nucleus` is a
library, not a CLI, so it ships no `completion` subcommand -- it returns the
script as a string and the host decides how to surface it.

```cpp
engine.register_element(nucleus::element("logging", nucleus::anchor::root()));
engine.register_element(nucleus::enum_element(
    "level", nucleus::anchor::keyspace("logging"),
    {"debug", "info", "warn", "error"}));

std::cout << engine.generate_completion(nucleus::shell::bash, "mytool");
```

```bash
_mytool_complete()
{
    local flags='--logging --logging-level --server --server-port'
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

This is the first release, and its scope is deliberately narrow.

* **Sources**: XML documents plus argv and env. Other document formats (YAML,
  INI, JSON) are not yet implemented -- the `source` seam is the extension point,
  but only the XML module ships.
* **Completion** is static, for **bash and zsh** only. There is no dynamic
  (runtime) completion and no fish support; both are future single-file additions
  through the same emission seam.
* **No serialization, mutation, or merge** of a resolved `configuration` yet. The
  resolved value is read-only; producing a new configuration as a value (clone,
  transfer, diff) is the next body of work.
* **Capability gating** is host-driven: the schema model expresses presence and
  selector role per element but does not yet carry per-element capability
  requirements the resolve fold could gate a source against automatically.
* **Cross-platform** support (macOS, Linux, Windows; `std::format` with a `{fmt}`
  fallback) is verified by the CI matrix.

The public API may still change while the engine stabilizes.

## License

Apache-2.0. See [`LICENSE`](LICENSE).

## Conventions

See [`CONVENTIONS.md`](CONVENTIONS.md).
