# nucleus conventions

These mirror the vagus-core conventions so that a future thin adapter and any
shared idioms stay frictionless. Where this file is silent, follow modern,
idiomatic C++ as practiced by libraries like asio.

## Language

- Idiomatic, cross-platform **C++20**. Must compile on macOS, Linux, and Windows.
- Lean on language features; avoid hand-rolling what the standard library offers.

## Naming & structure

- Full `snake_case` for types, functions, variables, namespaces. No `I`-prefix on interfaces.
- Root namespace `nucleus`. Sub-namespaces follow directory structure.
- One public concept per header where practical; small files (~100 LOC target,
  200 max), small functions (5-15 LOC, 25 max). Readability over dogmatic
  SOLID/DRY; never split a unified concept just to hit a number.

## Headers

- **Header guards, not `#pragma once`.** Format: `HPP_GUARD_NUCLEUS_<FOLDER>_FILENAME_H`.
- Do **not** add a trailing `// namespace x` comment after a closing namespace brace.
- Do **not** add a trailing comment after the include-guard `#endif`.

## Include order

Three major sections, separated by a single blank line:

1. Internal project includes (`#include "..."`)
2. Third-party libraries (`#include <...>`)
3. Standard library headers (`#include <...>`)

Within a section, group by folder location (intermediate groups separated by a
blank line). Within each group, sort by length first, then alphabetically.

## Ownership topology (load-bearing)

- Ownership tracks **composition/lifetime, not who-calls-whom.** The registries
  (schema, tokenizer, source) are flat sibling members of the top-level facade;
  none owns another.
- Registries collaborate by **hand-off** -- they are passed each other (a
  transient resolution context) at call time. **Invariant:** no registry stores
  a member reference/pointer to another registry; cross-registry needs are
  parameters.

## Layering

- `nucleus` is **mechanism**; ownership/reservation/naming policy and document
  discovery conventions belong to the **host/adapter**, never the core.
- `nucleus` is **document-format-agnostic**: formats are pluggable `source`
  modules, never baked into the engine.
