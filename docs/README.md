# nucleus API reference

This reference describes the public API of nucleus along three axes. The split
mirrors how the engine is meant to be consumed: most programs only ever touch the
first group; hosts that add a format or a policy reach for the second; the third
documents the parts nucleus ships that satisfy those seams.

| Document | Scope |
| --- | --- |
| [Types you use](api-using.md) | The user-facing vocabulary a host instantiates and reads: the facade, the schema, the resolved configuration, the built-in sources, diagnostics. |
| [Seams you extend](api-extending.md) | The base classes, concepts, and policies a host inherits from, composes, or adheres to: `source`, the `Parser` concept, `log_sink`, `registration_policy`, the capability/feature-gate model, discovery. |
| [Shipped implementations](api-implementations.md) | The concrete types nucleus ships that satisfy the seams: `env_source`, `argv_source`, the XML document source, the `log_sink` adapters, and `parser_adapter`. |

The [`examples/`](../examples) directory holds a small, self-contained program per
concept; each reference section points at the matching example.

## Orientation

nucleus resolves configuration from many sources -- a command line, documents,
the environment -- onto one hierarchical keyspace of `/`-separated key paths. A
registered **schema** is the single upstream authority: it dictates both the
command-line surface and the document structure, so a source never decides what
is admissible.

The lifecycle has two phases. A `configuration_space` is **configurable**
(`register_element` / `register_*` / `install_tokenizer`) until `load()` or
`resolve()`, which folds the sources, validates against the schema, expands
`${...}` tokens, and yields an immutable, freely thread-readable `configuration`.
Registration after resolve is a state-machine error.

```
configuration_space  --register_*-->  configuration_space  --load()/resolve()-->  configuration
   (configurable)                         (configurable)                            (resolved, immutable)
```

The core carries no policy: ownership, reservation, filename conventions, and
which environment variable maps to which key are all the host's to decide. The
core provides the mechanism -- identity-tagged registration, referential
integrity, conflict and provenance reporting, capability gating, and the seams
below.
