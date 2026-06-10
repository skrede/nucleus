# Conventions

The rules a change is reviewed against. Several are enforced mechanically
(named below); the rest are enforced in review.

## Language and platforms

* C++20, no extensions (`CMAKE_CXX_EXTENSIONS OFF`). Code must build and pass
  tests on Linux, macOS, and Windows (GCC, Clang, AppleClang, MSVC) — the CI
  matrix is the arbiter.
* American English in identifiers, comments, messages, and docs.

## Naming

* `snake_case` for everything: types, functions, variables, concepts,
  enumerators, namespaces, file names. Members carry an `m_` prefix. No
  CamelCase, no Hungarian.
* Files are named after the primary type they declare (`source_stack.h`
  declares `source_stack`).
* Module naming: source types live flat in `nucleus::` with a module-prefixed
  name (`xml_source`, `env_source`, `argv_source`, `runtime_source`); operations
  scoped to one module live in that module's namespace
  (`nucleus::xml::emit_document`); implementation details live in `detail`
  namespaces. Each module's public headers install under `nucleus/<module>/`.
* Enums map to text through an exhaustive `switch` with no `default:` and an
  `"unknown"` return after it, so adding an enumerator warns at every mapping.

## Formatting

`.clang-format` (derived from the project's CLion code style) is authoritative
for layout; the include order below is the one rule it cannot express, so
`SortIncludes` is off. The shape it encodes:

* 4-space indent, spaces only; continuation and constructor-initializer lines
  indent 8.
* No enforced column limit: authored line breaks are preserved. Wrap by
  judgment and match the surrounding code.
* Braces on the next line for functions, classes/structs/enums/unions, control
  statements, and case blocks; `else`/`catch` on their own line. Namespace
  braces stay on the declaration line, and namespace members are not indented.
* One enumerator per line.
* `&` and `*` bind the declarator: `const std::string &name`.
* `template<typename T>` without a space; no space before statement or call
  parentheses (`if(cond)`); a space before the range-for colon
  (`for(const auto &x : xs)`).
* Constructor initializer lists break before the colon; when chopped, the
  break goes before each comma.
* Consecutive assignments and trailing comments may be column-aligned.

## Headers

* Header guards, never `#pragma once`, on the format
  `HPP_GUARD_<NAMESPACE>_<FOLDER>_FILENAME_H`. No comment on the closing
  `#endif`, and no `// namespace x` comment on a closing brace.
* Include order: project includes (quoted) first, third-party second, standard
  library third — one blank line between these sections. Within a section,
  group by folder with a blank line between groups; within a group, sort by
  line length, ties alphabetically. `.clang-format` sets `SortIncludes: Never`
  so the formatter cannot fight this rule.
* A public header (`lib/<module>/include/`) must never include a private one
  (`lib/<module>/src/`). Enforced: `public_surface_test` compiles every public
  header with only the public include roots.
* The core is format-agnostic: nothing under `lib/core/` may name a document
  format or include a module adapter header. Enforced: `core_purity_check`
  (CTest gate and `scripts/core_purity_check.sh` in CI).

## Comments

The default is no comment — names and signatures carry the meaning. Write one
only when it does something code cannot:

* cites a source (paper, RFC, standards clause, bug report, URL);
* names a non-obvious algorithm or technique so a reader can look it up;
* states a real but invisible contract, invariant, ordering, side effect,
  tradeoff, or workaround — in one or two sentences.

Never restate a name, signature, or control flow; never narrate sections.
A comment that says what the next line already says is noise: delete it.

## Errors

* No exceptions across the public API. Every fallible operation returns
  `nucleus::expected`; failures carry `nucleus::error` — an `errc` code naming
  the failure class plus the verbatim human-readable reason. New failure paths
  attach the correct `errc`.
* `expected` is `[[nodiscard]]`: every result is checked, including in tests
  and examples.
* Errors are loud and named. Never degrade silently, never swallow a failure,
  never report a misleading class (a syntax error is not a range error).
  Host-supplied callables (converters, registration policies) traffic in plain
  message strings; the engine attaches the code at the seam.

## Design

* Follow the idiom of the standard library, asio, and boost: value semantics,
  RAII, concepts at extension seams (with type erasure where heterogeneity is
  needed), mutable-builder-to-immutable-product lifecycles. Prefer a policy or
  concept seam over inheritance only when it genuinely wins, not dogmatically.
* Entry points borrow, they do not consume: a `const configuration_space &`
  and a reusable `source_stack &`.
* Mechanism in core, policy in host: the core imposes no vocabulary,
  reservation, or namespacing rules of its own.
* Domain-neutral vocabulary everywhere a user can see (code, docs, examples,
  tests): generic names like `server`, `cluster`, `host`, `port`. Never
  reference any particular embedding application.

## Tests

* Catch2. Every behavior change carries a test; every error path asserts both
  the `errc` and the message content a user would see.
* Tests assert behavior — values, provenance, error identity — not
  implementation detail.
* A verification claim needs a falsifier: sanitizer builds carry trip tests
  that prove the tool is armed, golden suites carry an intentional-divergence
  fixture, the version is pinned by a test against all of its declarations.
  When you add a guarantee, add the check that fails when it stops holding.

## Commits and branches

* Message format, with one logical change per commit:

  ```
  {Prefix}: {summary sentence}.

  - {what was done}
  - {another item if applicable}
  ```

  Allowed prefixes: `Feature`, `Fix`, `Refactor`, `Docs`, `Examples`,
  `Optimization`, `WIP`. Use `WIP` if the commit does not compile.
  Single-item commits may omit the bullet list.
* No issue-tracker IDs, project-management keys, or planning references in
  commits, code, comments, docs, or examples.
* Branching: `master` (releases) &larr; `develop` (integration) &larr;
  `milestone/<version>` (work). Work lands on the milestone branch and merges
  milestone &rarr; develop &rarr; master. `develop` is never deleted. CI runs
  on pushes to `develop` and `milestone/**` and on pull requests into `master`.
* Never tag, merge, or force-push around `.gitignore`.

## Versioning

* Pre-1.0 SemVer: a minor bump may break. The CMake package writes
  `SameMinorVersion` compatibility accordingly.
* The version is declared in `project()`, the `version.h` macros, and
  `version()`; `version_test` pins all three together — bump all or fail.
