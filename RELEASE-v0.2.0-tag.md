Tag: v0.2.0

Release title: v0.2.0 — first public release

Release notes:

The first public release of nucleus — a C++20 configuration library built around
a coherent vocabulary, an immutable builder → space → configuration lifecycle,
capability gating and document emission, and a value-semantic C++20 concept for
configuration sources.

## What's in it

- **`nucleus::expected<T, E>`** — a result type with the full
  `std::expected`-matching surface (four ref-qualified monadic overloads, `void`
  specialization) behind a one-line swap alias.
- **Immutable lifecycle + converter registry** — a `configuration_space_builder`
  seals into an immutable `configuration_space`; a free `load` mints immutable
  configurations without mutating the space; `expand()` deep-copies, so there is
  no shared base and no ref-count races. A `type_index`-keyed converter registry
  sits as a flat sibling.
- **Source seam** — `configuration_source` is a C++20 concept; sources are
  non-intrusive plain structs carried by a move-only erased `source_handle`; a
  variadic, last-listed-wins `source_stack` composes into the free
  `load(space, stack, options)`. The space owns no sources, sources know neither
  space nor configuration, and a produced `configuration` is a disconnected,
  self-owning snapshot that outlives the inputs that built it.
- **Modular sources** — XML, env, args, and an in-memory `runtime_source`, each in
  its own per-target `lib/` tree. Core links only fmt; env and args are opt-in
  modules.
- **Cross-source precedence by stack position** — precedence is decided purely by
  where a source sits in the stack. Documents rank at the base, so any stack
  source overrides them (defaults < files < env < args). A per-entry
  inheritance-layer ordinal handles within-document base → derived layering
  independently of rank.
- **Capability gating** — per-element capability requirements auto-gate the
  resolve fold, so a source can only contribute what the schema permits.
- **Document emission** — schema → XML template emission and configuration → XML
  persistence through a format-agnostic `config_emitter` concept, with pugixml
  quarantined to the xml module.
- **Inheritance grammar** — base → derived inheritance proven across three
  topologies, with typed and repeated elements supported and round-tripped.

## Verification

- Main build: **59/59** tests pass.
- AddressSanitizer build: **60/60** (parallel and serial), including source-seam
  disconnection (a `configuration` outlives a dropped stack + space + arena).
- Output is regression-locked by a CI-gated golden-fixture suite.

## Known limitations / planned next

Positional document placement and reserved cross-source resolution directives;
meta-schema selectors; a compile-time `source_stack<Policies...>` storage type;
valid-floor document emission; clone/COW configuration; YAML/INI/JSON codecs.
