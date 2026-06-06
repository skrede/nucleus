# nucleus

A document-format-agnostic configuration engine for C++ — schema, tokenization,
validation, a unified keyspace, and argument parsing, all driven by registered
schemas.

## What it is

`nucleus` treats a configuration as a flat **keyspace** (`/`-separated,
FQN-style key paths). Command-line flags (`--a-b-c=v`), XML elements, and other
document formats all map onto the *same* keyspace, and a registered **schema**
is the single upstream authority that dictates both the CLI surface and the
document structure.

The engine is **format-agnostic**: XML/YAML/INI/argv/etc. are pluggable `source`
modules, not built-in assumptions. It is also **policy-free** at the core —
ownership, reservation, and filename conventions live in the host/adapter that
embeds it.

It is built standalone and carries no application-specific coupling, so it is
useful to any C++ program.

## Status

The core engine is substantially implemented:

- A two-phase **facade** (`configuration_space`) that is configurable
  (`register_schema` / `register_element` / `register_tokenizer` /
  `register_source` / `install_tokenizer`) until `load()` / `resolve()`, which
  yields an immutable, freely thread-readable `configuration`.
- A typed **schema** model (`anchor::root` / `anchor::keyspace`, required,
  identity, and closed-value-set elements) that is authoritative over both the
  CLI surface and the document structure, with referential-integrity enforcement
  at attach time and value-set validation at resolve.
- The **source** seam (`source` / `provider`) with argv, env, and a separately
  linked XML module (wrapping pugixml, privately linked and unreachable from the
  core), plus extension-based parser discovery.
- The **tokenizer** pipeline (`${...}` syntax) with the generic core tokenizers
  (env, uuid, file/dir/self, string, scope), recursive-to-fixpoint nested
  expansion with depth and cycle guards, and an opt-in `HOST` tokenizer module.
- **Diagnostics**: nearest-key suggestions, conflict/provenance reporting, and a
  no-op `log_sink` seam (`std::format`, with an `fmt` fallback for toolchains
  that lack `std::format`).
- **Shell completion** generation (`generate_completion`) that projects the
  schema into a static bash or zsh completion script -- flag names plus each
  element's declared value set -- behind a per-shell emission seam, so each shell
  owns its own quoting and word-break handling. The same flag mapping the CLI
  surface uses drives the projection, so completion cannot drift from the CLI.

The public API may still change while the engine stabilizes.

## License

Apache-2.0. See [`LICENSE`](LICENSE).

## Build

```sh
cmake -B build -DNUCLEUS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Conventions

See [`CONVENTIONS.md`](CONVENTIONS.md).
