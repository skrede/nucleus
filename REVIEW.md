# Repository review — nucleus

Date: 2026-06-10. Scope: full repository on `master` (335e358) — library (`lib/`),
tests, examples, build system, CI, packaging, documentation, naming. All findings
below were verified against the code; the four correctness majors and every
release blocker were independently confirmed at the cited locations.

## Verdict

The core architecture is genuinely strong: the concept-based source seam with
external-polymorphism erasure, the view-or-owned value model with arena pinning
and copy-out at the load boundary, and the builder-to-sealed-space split are
better engineered than most shipping configuration libraries. Semantic test
coverage is well above average (~91% line coverage, measured locally).

What stands between the current state and a world-class bar is not the design:
the installed-consumer view of the library is never compiled anywhere, and
several verification loops verify nothing. Tagging v0.2.0 from this tree ships a
package whose public headers do not compile downstream, a README whose quickstart
uses a retired API, a version that reads 0.0.0, a coverage badge fed by empty
reports, and a thread-safety claim no tool has ever checked.

## Release blockers

### B1. Public header includes a private header — installed consumers cannot compile

`lib/core/include/nucleus/schema/capability_requirements.h:7` includes
`nucleus/schema/schema_registry.h`, which lives in `core/src/` and is never
installed (`lib/CMakeLists.txt:61` ships only `include/`). Masked in-tree because
test targets add `src/` to their include path.

Related uncallable public surface:
- `configuration_space_builder::install_tokenizer(tokenizer)` is public API but
  `tokenizer` has no public header — unconstructible from an installed package.
- The free `generate_completion(shell, const schema_registry &, std::string_view)`
  (`completion/completion.h:38`) takes an internal type.

Root cause: no CI job compiles the pure-consumer view. The install-tests all run
with `-DNUCLEUS_BUILD_SOURCE_XML=OFF` and link only `nucleus::nucleus` +
`nucleus::runtime`, so the riskiest export paths (pugixml `$<LINK_ONLY:>`,
fetched-fmt vendoring, `nucleus::xml`) are never verified.

### B2. README and docs/ document the previous API generation

- `README.md:97-112` (quickstart) uses `configuration_space engine;`,
  `engine.register_element(...)`, `engine.load({args},{paths},make)`,
  `std::unique_ptr<nucleus::source>`, and `xml_source::from_string(...)` — none
  exist in the current builder / sealed-space / free-`load` surface.
- `docs/api-*.md` reference ~15 headers that do not exist
  (`nucleus/source/source.h`, `nucleus/result.h`, `nucleus/entry/precedence.h`, ...)
  and document a retired virtual `source` base class and `parser_adapter`.
- `README.md:194-213` "Limitations" denies two features that shipped: capability
  auto-gating (`capability_requirements.h`, `check_capabilities`) and XML
  serialization/round-trip (`config_emitter.h`, `xml_persist_test`).
- `README.md:219-221` links `CONVENTIONS.md`, which is gitignored and absent.

The `examples/` are current (spot-checked against headers) — regenerate docs from
them.

### B3. Version is 0.0.0 everywhere while v0.2.0 is being prepared

`CMakeLists.txt:2` (`project(nucleus VERSION 0.0.0)`), `version.h` macros, and
`version.cpp` all say 0.0.0; the untracked `RELEASE-v0.2.0-tag.md` declares
v0.2.0 (with stale test counts). A downstream `find_package(nucleus 0.2)` fails
against the installed package-version file. The version is hand-duplicated in
three places with no generation and no test pinning it. Also
`COMPATIBILITY SameMajorVersion` at major 0 treats every 0.x as compatible;
`SameMinorVersion` is the honest pre-1.0 choice.

### B4. Coverage upload has been empty since the lib/ restructure

`.github/workflows/linux.yml:84-85` filters `include/nucleus/` and
`src/nucleus/`, anchored at the repo root; sources live under `lib/*/...`, so
gcovr matches zero files (reproduced locally with gcovr 8.6: "All coverage data
is filtered out", TOTAL 0). `fail_ci_if_error: false` plus informational-only
Codecov statuses hid it. Fix: `--filter 'lib/'` (and note gcovr 8.6 trips on a
gcov negative-hits artifact in `keyspace.h` — add
`--gcov-ignore-parse-errors=negative_hits.warn_once_per_file`). The
instrumentation itself is honest; real coverage is ~91% line / 56% branch.

### B5. CI never sees the integration flow

All three workflows trigger on `master` only. The branching model
(milestone -> develop -> master) means all integration happens on branches with
zero CI until the final merge. Add `develop` (and optionally `milestone/**`) to
both trigger lists.

## Correctness

### Major — triggerable by document content

- **Unbounded recursion on XML nesting depth.**
  `lib/xml/src/nucleus/xml/xml_source.cpp:90-199`: `walk()` recurses once per
  nesting level with no depth limit; a few hundred KB of `<a><a><a>...` overflows
  the stack and crashes the process. The codebase treats this failure class as
  in-scope elsewhere (`chain_walker` has `depth_guard`, token expansion has
  `expansion_guard`); this is the one unguarded recursion over untrusted input.
- **CDATA leaf values are silently dropped.**
  `xml_source.cpp:39-46` (`is_text_leaf`) and `:51-63` (`keyed_value`) accept
  only `node_pcdata`; pugixml parses `<key><![CDATA[v]]></key>` as `node_cdata`,
  the element falls through to the structural walk, and the value vanishes with
  no error. CDATA is the standard idiom for values containing `<` or `&`; the
  observable failure is a confusing "required field missing" or a silently used
  lower-precedence value.

### Major — contract contradictions reachable through load()

- **`runtime_source` gate/fold contradiction.**
  `runtime_source.h:43-58` declares `duplicate_keys` at source level (so
  `check_capabilities` admits a repeated-element schema), but `pull()` stamps
  every entry with `capability_descriptor{}`, and the fold's duplicate check
  (`resolution_context.h:151-160`) reads the per-entry descriptor. Two values on
  a repeated path pass preflight, then fail load with an error stating the source
  "does not support duplicate_keys" — contradicting its own descriptor. Untested:
  no test sets two values per repeated path on a runtime source.
- **`${file.*}` / `${dir.*}` / `${self.*}` tokens are unreachable through load().**
  The fold calls the frame-less `resolve_tokens` overload
  (`resolution_context.h:138`); nothing in the load path calls `push_file_frame`
  (only `token_resolution.h` and tests do). The chain walker knows each
  document's path but never threads it through. Any document using the
  documented file-location tokens fails the entire load with
  `out_of_scope_context`. Loud, but an entire documented token category cannot
  work end to end. (Open question: deliberately deferred wiring, or omission.)

### Minor

- `recognizer_of` closure dangles after copy-assignment to the space
  (`configuration_space.cpp:358-364` captures `&m_impl->schema`;
  copy-assignment replaces `m_impl` while the space object is alive — contradicts
  the "valid as long as the space outlives it" contract; moves are safe).
- The `flag_of` hyphen bijection (`cli_flag.h:14-25`) is asserted but never
  enforced: nothing rejects hyphens in element names, so an element `log-level`
  emits `--log-level`, which `normalize_arg` maps back to the undeclared path
  `log/level`. Completion, CLI, and schema silently diverge.
- argv `lenient` policy is dead on the wired path: a recognizer exists only when
  a schema surface exists, and a non-empty surface makes `validate()` hard-fail
  any undeclared path (`resolution_context.h:555-560`) — the documented
  "warn and store anyway" behavior cannot survive a load.
- env/argv `emit_document` claims values carry no embedded newline but nothing
  enforces it (`env_emitter.h:68-73`, `argv_emitter.h:68-73`); a value with `\n`
  silently corrupts the emitted line format.
- XML emit/read is not a round trip when both `a/b` (scalar) and `a/b/c` exist:
  `xml_emitter.cpp:98-104` reuses the value-carrying `<b>` as a container,
  producing mixed content whose scalar is dropped on re-read.
- XML primary-key attribute values containing `/` splice extra path segments
  (`xml_source.cpp:187`); `slice()` then buckets the strain on the first segment
  only, so selection of the full value can never match.
- `std::toupper`/`std::tolower` passed as function pointers
  (`builtin_tokenizers.cpp:23-29, 74-81`) — unspecified since C++20
  ([namespace.std]/6); wrap in a lambda.
- `${string.substr}` argument parsing: `std::stoull("-1")` wraps instead of
  throwing, so a negative count silently returns the remainder; 32-bit `size_t`
  truncation turns out-of-range into valid-looking values
  (`builtin_tokenizers.cpp:99-116`).
- Floating-point fallback (`converters.h:88-124`, Apple/libc++ path) accepts C99
  hex floats that `std::from_chars` rejects — same input converts on Apple,
  fails elsewhere.
- `nucleus::detail::expected` (variant-backed) can become
  valueless-by-exception — a third state `std::expected` cannot reach — and its
  checked accessors throw `std::bad_variant_access`, not
  `std::bad_expected_access<E>`, so the promised drop-in C++23 migration changes
  observable behavior.
- Completion emitters interpolate `model.prog` unsanitized into
  `complete -F ...` / `#compdef ...` lines (`bash_emitter.h:85`,
  `zsh_emitter.h:30,43`) despite otherwise careful single-quote escaping.
- `xml_source.cpp:225` collapses file-read failure and parse failure into one
  generic "failed to parse input" — a typo'd path reports as a parse error.

### Checked and sound

Buffer/`string_view` lifetimes through fold and freeze (the headline claim
holds); iterator invalidation; chain-walker depth/cycle guards;
char-signedness casts (`unsigned char` everywhere); locale-free numeric parsing
on the `from_chars` path; the const-space concurrent-read design itself.

## API design

- **`load`/`check_capabilities` consume the stack.** Both take `source_stack` by
  value (`configuration_space.h:200-208`); `source_handle` is move-only and
  `layers()` has no const overload, so the documented "pre-flight then load over
  the same stack" sequence is impossible — callers must rebuild the stack.
  `check_capabilities` only reads `capabilities()`; it should take
  `const source_stack &`.
- **Errors are stringly-typed end to end.** Every result channel carries
  `std::string`; `get_as` documents three semantically distinct failures
  distinguishable only by parsing prose. The one design decision worth revisiting
  before the API calcifies — pervasive and hard to retrofit.
- **Fallible mutators are discardable.** No `[[nodiscard]]` on `register_*` /
  `install_*` / `set_registration_policy`, despite the "LOUD state-machine
  error" contract. One-line fix: mark `class expected` itself `[[nodiscard]]`.
- **`converters.h:158-436`** is ~280 lines of copy-paste: ten near-identical
  specializations where one `template<std::integral T>` plus a
  `std::floating_point` overload belongs. It also misdiagnoses `"+5"` as
  "out of range" instead of a syntax issue, and is the heaviest compile-time
  offender in the public surface (fully inline, `std::function`-returning).
- Smaller: `expected<std::monostate, std::string>` where the existing
  `expected<void, E>` specialization would do; `anchor::keyspace()` silently
  collapsing malformed paths to root; `configuration` lookups taking
  `const std::string &` over a map without transparent comparators;
  env/argv emitters duplicating `is_leaf` verbatim with O(N^2) lookup; duplicate
  forward declaration of `enum class shell` with no fixed underlying type
  (ill-formed NDR if the definition ever gains one); `version()` hardcoded;
  `discovery` as a class of two static functions instead of free functions;
  `owner_token` heap-allocating per default-constructed token, with a `noexcept`
  `operator==` routing into potentially-throwing host comparisons.

## Naming

Consistent and good: uniform snake_case with `m_` members across all 94 files;
all 69 header guards exactly match the `HPP_GUARD_...` format; the schema element
factory family (`typed_element`, `required_element`, `primary_key_element`, ...)
is coherent and guessable.

Inconsistencies:
1. Namespace structure is incoherent across source libs: `xml_source` is in
   `nucleus::xml`; `env_source`, `argv_source`, `runtime_source` are flat in
   `nucleus`; emitters are in `nucleus::xml` / `nucleus::env` / `nucleus::args`.
   The env and args source/emitter pairs straddle two namespaces.
2. Include path does not mirror namespace: all source libs install under
   `nucleus/sources/` but no `nucleus::sources` exists; the lib directory is
   `args/` while every identifier says `argv`.
3. "entry" means two things in public paths: `nucleus/entry/` (the load entry
   point) vs `keyspace/entry.h` (a keyspace row). The private tree compounds it:
   `src/nucleus/entry/` holds the resolution machinery and
   `configuration_space.cpp`, asymmetric with the public layout.
4. `env_emitter_detail` / `argv_emitter_detail` instead of the project's
   established `detail`.
5. `to_string(capability)` falls back to `"nesting"` and `to_string(log_level)`
   to `"info"` on out-of-range values — mislabeling instead of `"unknown"`.
   Test codename "trident" (`system_trident_disconnect_test.cpp`) is never
   defined anywhere.

## Tests

Well above average: 327 cases that overwhelmingly assert behavior (values,
provenance, exact error text); exceptional semantic negative-path coverage
(`inherit_chain_test.cpp` covers depth-cap boundaries, 2- and 3-file cycles,
cross-layer duplicate keys); rare self-verifying machinery (ASan trip test
proving the sanitizer is armed, negative-compile `WILL_FAIL` fixtures, an
intentional-divergence golden fixture, completion scripts executed under real
bash including a quote-injection breakout test).

Blind spots:
- **The concurrency guarantee is effectively untested.**
  `concurrent_load_test.cpp` is 8 threads x 1 iteration, no start barrier, and
  its comment claims ASan validates "no data race" — ASan does not detect data
  races, and there is no TSan anywhere in CI or `cmake/buildflags.cmake`.
  Fix: one Linux TSan job, a `std::latch` start barrier, ~100 iterations.
- Lexical XML failure paths are thin: no test feeds a nonexistent file path,
  empty document, or binary garbage; only one malformed-XML test exists.
- The golden suite's repeated-element serialization branch
  (`golden_runner.h:88-105`) is exercised by zero fixtures.
- `path_to_text` — whose reason to exist is a Windows-specific claim — has no
  direct test on the Windows leg.
- `completion_smoke_test.cpp:87-91` reports pass (not skip) where bash is
  absent; Catch2 v3.15's `SKIP()` would make the gap visible.
- Sanitizers are ASan-only, Linux-only; no UBSan (cheap to add); MSVC ASan
  (`/fsanitize=address`) is excluded by the `NOT MSVC` guard.

## Build / CI / packaging / hygiene

- No `POSITION_INDEPENDENT_CODE` on the static libs — linking nucleus into a
  consumer's shared library fails to relocate on Linux.
- Root-scope `add_compile_options` warnings leak into fetched fmt / pugixml /
  Catch2 builds, including `/permissive-` on MSVC.
- `nucleus::source_xml` is documented (`docs/api-implementations.md:96`) but
  exists only as a build-tree alias; the installed export defines `nucleus::xml`.
- When fmt/pugixml are fetched (not found), `cmake --install` vendors them into
  the consumer's prefix — undocumented; `find_dependency` calls carry no minimum
  versions.
- FetchContent tags are not pinned to commit hashes; `pip install gcovr` is
  unpinned.
- No dependency caching in CI (deps re-cloned and rebuilt every run).
- Linux/macOS ctest has no `--timeout` (a deadlock hangs to the job limit);
  Windows's `--timeout 30` is flake-tight for negative-compile tests that run a
  nested `cmake --build` on a loaded runner.
- `CMAKE_BUILD_TYPE` force-defaulted to Debug with no notice.
- Root `CMakeLists.txt:31-33` re-sets `CMAKE_CXX_STANDARD` already set by
  `buildflags.cmake`; the "for the probe" comment is misleading.
- Every XML example adds `lib/core/src` to its include path but none uses a
  private header — a vestigial crutch misrepresenting the consumer model.
- No "consuming nucleus" documentation: neither README nor docs/ shows
  `find_package(nucleus)` + `target_link_libraries(... nucleus::xml ...)`.
- `compile_commands.json` untracked and not gitignored; `.gitignore` has a
  `*.gdca` typo (should be `*.gcda`) and is mostly inherited Qt cruft;
  `build-asan/` contains stale pre-restructure artifacts.
- `format.h` re-probes `__cpp_lib_format` per consumer TU while the fmt link is
  decided once at library configure time — a mixed-toolchain consumer can pick a
  different backend than the built core.

## Priority order

1. Compile the consumer view: fix `capability_requirements.h`, resolve
   `install_tokenizer`'s uncallable surface, enable XML + fetched-fmt in at
   least one install-test.
2. Rewrite README quickstart and docs/ from the current examples; fix the false
   Limitations claims; drop the dead `CONVENTIONS.md` link.
3. Single-source the version and set it to the release value.
4. Fix gcovr filters; add a TSan job and make the concurrency test capable of
   failing.
5. Add `develop` / `milestone/**` to CI triggers.
6. Fix the four correctness majors: XML depth guard, CDATA leaves,
   runtime_source per-entry capabilities, file-scope token wiring (or an
   explicit ruling that it is deferred).
7. Before the API calcifies: typed errors vs strings; `[[nodiscard]]` on
   `expected`; const-borrowing `check_capabilities`.
8. Naming: unify the source-lib namespace scheme and the args/argv split;
   resolve the entry/entry collision.
