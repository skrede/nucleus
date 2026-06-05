# nucleus

A document-format-agnostic configuration engine for C++ — schema, tokenization,
validation, a unified keyspace, and argument parsing, all driven by registered
schemas.

> **Status: scaffold.** The public API is not defined yet. This repository
> currently contains only the build/convention skeleton and a version
> walking-skeleton. The design has been captured in
> [`.planning/HANDOFF.md`](.planning/HANDOFF.md); the project shape is intended
> to be established through `gsd-new-project` using that brief as seed context.

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

It is built standalone and is intended, over time, to replace the configuration
system currently embedded in vagus (via a future thin `vagus-nucleus` adapter),
but it carries no vagus-specific coupling.

## Build

```sh
cmake -B build -DNUCLEUS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Conventions

See [`CONVENTIONS.md`](CONVENTIONS.md).
