# nucleus API reference

This reference describes the public API of nucleus along three axes. The split
mirrors how the engine is meant to be consumed: most programs only ever touch the
first group; hosts that add a format or a policy reach for the second; the third
documents the parts nucleus ships that satisfy those seams.

| Document | Scope |
| --- | --- |
| [Types you use](api-using.md) | The host-facing vocabulary a program instantiates and reads: the builder and the sealed space, the schema, `load()` and its options, the resolved configuration, source stacks, emitters, diagnostics, completion. |
| [Seams you extend](api-extending.md) | The concepts a host makes a type satisfy and the policies it composes: the `config_source` concept and its optional affordances, capability descriptors and gating, inheritance, custom tokenizers and converters, `registration_policy`, `log_sink`, discovery. |
| [Shipped implementations](api-implementations.md) | The concrete modules nucleus ships that satisfy the seams: `xml_source`, `env_source`, `argv_source`, `runtime_source`, the per-format emitters, and the `log_sink` adapters — with the capability descriptor and CMake target of each. |

The [`examples/`](../examples) directory holds a small, self-contained program per
concept; each reference section points at the matching example.

## Orientation

nucleus resolves configuration from many sources -- a command line, documents,
the environment, code -- onto one hierarchical keyspace of `/`-separated key
paths. A registered **schema** is the single upstream authority: it dictates
both the command-line surface and the document structure, so a source never
decides what is admissible.

The lifecycle has two phases. A `config_space_builder` accepts
registrations (`register_element` / `register_*` / `install_tokenizer`) until
`build()` seals it into an immutable `config_space`. The free function
`nucleus::load(space, source_stack, load_options)` gates the stack's
capabilities against the schema's shape, folds the sources, validates, expands
`${...}` tokens, converts typed values, and yields an immutable, freely
thread-readable `config`. Registration after `build()` is a
state-machine error, and one sealed space serves any number of loads.

```
config_space_builder  --register_*-->  build()  -->  config_space (sealed)
                                                                  |
                          nucleus::load(space, stack, options)  --+-->  config (resolved, immutable)
```

The core carries no policy: ownership, reservation, filename conventions, and
which environment variable maps to which key are all the host's to decide. The
core provides the mechanism -- identity-tagged registration, referential
integrity, automatic capability gating, conflict and provenance reporting, and
the seams above.
