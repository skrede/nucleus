# Changelog

All notable changes to `nucleus` are recorded here. This file is written for the
upgrader: each entry names a behavior that *used to be silent* and is now *loud*,
so you can predict what a previously clean load or build will now reject &mdash; and
why. Entries describe behavior, not internals.

## 0.4.2 (unreleased)

This release makes the emitters honest. Where a previous version would render
something other than what was resolved &mdash; an ambiguous record, an invalid XML
name, a collapsed instance ordinal &mdash; the emitter now reports before producing
any output. Every entry below can turn a previously clean emit into a failure. One
entry narrows what a document may contain at all; read that one first.

Two groups after the emitter ones do not concern emission at all: the schema API
carries one breaking rename, and what nucleus requires to build and what an
installed package hands a consumer both changed. An upgrader who does not emit
still wants those.

### Instance ordinals

- **An instance ordinal above 4294967295 is now rejected.** The bracket grammar
  previously accepted up to 18 digits, but the engine represented an ordinal in
  `std::size_t`, so on a 32-bit build every ordinal past `UINT32_MAX` saturated to
  the same value: two distinct instances compared equal, matched the same anchor,
  and rendered identically. The accepted domain is now 1 to 10 decimal digits with
  a value no greater than 4294967295 &mdash; what every supported platform holds
  losslessly &mdash; and anything larger fails as a malformed key path naming the
  source and the path. This is the one change here that can reject a document that
  loaded before. Nothing else in the release narrows an input domain.

### Flat emission (argv and env)

- **A key containing `=`, a carriage return, or a line feed is now rejected.** Such
  a key had no unambiguous flat spelling: `bad=key` rendered as the record
  `bad=key=value`, which reads back as the key `bad` with the value `key=value`. It
  is now refused both when the configuration is constructed from a raw map and when
  an emitter is asked to render it, in each case naming the offending key. Values
  are unaffected &mdash; `=` remains ordinary data to the right of the split.
- **An argv space name or CLI delimiter carrying those bytes is now rejected.** The
  space name is concatenated into the flag prefix and was not checked; a delimiter
  carrying a newline broke the record the same way a key did.
- **Flat template rendering now validates.** Template output previously ran no
  preflight at all &mdash; not even the line-break check the document path already
  performed &mdash; so a template could emit a record no reader could parse back.

### XML emission

- **Element names and text are validated against XML 1.0 before anything is
  emitted.** Malformed UTF-8, a name that is not a valid `NameStartChar`/`NameChar`
  sequence, and code points outside `Char` (including U+FFFE and U+FFFF) previously
  produced a success result and an unparseable document. They now report.
- **XML template rendering now validates at all.** Template emission previously
  checked nothing, including the wrapper element, despite the document path
  validating it. A schema element whose name carries bracket-index notation is now
  reported rather than emitted as the invalid name `<node[0] />`.

### Delivery

- **`unwritable_destination` is documented as the delivery failure it is.** An
  emitter renders and validates a complete document in owned storage before
  delivering it; a failure to accept those bytes is reported as
  `unwritable_destination`, distinct from the renderer's `malformed_source`. An
  emitter never flushes: success means the stream buffer accepted every byte, and a
  later flush, close or persistence failure is the caller's and lies outside the
  emitter result. A short write's accepted prefix is not rolled back.

### Schema API

- **`merge_mode::wholesale_replace` is now `merge_mode::replace_by_ordinal`.** The
  old enumerator is gone under every spelling &mdash; there is no alias and no
  deprecated form &mdash; so a translation unit that names it no longer compiles.
  This is a compile error, not a behavior change: the mode is still the default and
  still replaces, whole, each instance a higher layer supplies while leaving the
  lower layer's unaddressed instances in place. Only code that spelled the default
  explicitly is affected; a schema that never named a merge mode needs no edit. The
  new name states the axis the replacement runs on, beside its sibling
  `replace_by_key`. Six Catch2 tags moved with it, from `[wholesale_replace]` to
  `[replace_by_ordinal]`, so a local script that filters tests on the old tag now
  selects nothing and says nothing.

### Building and consuming nucleus

- **Building nucleus now requires CMake 3.25, up from 3.24; consuming an installed
  package still requires only 3.21.** 3.24 does configure and build nucleus without
  complaint, which is exactly the problem: it parses `FetchContent_MakeAvailable`
  without the `SYSTEM` keyword and drops it silently, so a fetched pugixml, `{fmt}`
  or Catch2 is compiled as an ordinary include rather than a system one. Its own
  headers' diagnostics then reach nucleus's curated warning set, and because
  warnings are errors by default for a top-level nucleus build, a warning in code
  that is not yours fails the build with nothing saying why. The declaration now
  matches the insulation the build actually relies on. Consuming an installed
  package is unaffected and keeps its own lower minimum.
- **An installed package now declares the formatting backend it was built with.**
  The install tree carries `nucleus/detail/format_backend.h`, defining
  `NUCLEUS_USE_STD_FORMAT` to the answer settled when nucleus was configured, and
  `nucleus/format.h` branches on that rather than on the consuming translation
  unit's own `__cpp_lib_format`. The two can disagree: a standard library that
  provides `std::format`, consuming an archive built against the `{fmt}` fallback,
  previously gave `nucleus::format` one meaning in your translation units and
  another in the archive's. A consumer now compiles against the archive's choice
  whatever its own library advertises.
- **`NUCLEUS_INSTALL` gates nucleus's install and export rules, and defaults off
  when nucleus is vendored.** A project that vendors nucleus and installs its own
  executables no longer has to keep a vendored dependency's install rules on just
  to satisfy an export set that dependency is not in. One shape still needs them
  on: if your own export set holds a static library that links nucleus, the private
  link survives in the exported interface and the identical export error moves one
  level up. Configure with `-DNUCLEUS_INSTALL=ON` in that case.

## 0.4.1

This release closes a large backlog of silent failure modes. In almost every case
the old behavior accepted, dropped, or mis-resolved something without a diagnostic
and the new behavior now reports it &mdash; a load, a schema build, an emit, or a
build that was previously accepted may now fail with a clear error. One entry (the
precedence-inversion fix) instead changes resolved values silently; it is called
out as such. Read the group that matches what you use before you upgrade.

### Tokenizer

- **Pass-1 substitution budget is now enforced.** A load that performed more than
  2500 first-pass `${...}` substitutions was previously expanded without bound;
  it now fails with a budget error. Note the divergence: the first pass is capped
  at 2500 while the second pass keeps its 10000 cap. A benign but genuinely large
  configuration that trips the first-pass cap is not a bug in your document &mdash;
  raise the ceiling with the `load_options.expansion_budget` knob (a value of `0`
  selects the 2500 default).
- **A duplicate named tokenizer argument is now rejected.** Passing the same
  argument name twice was previously accepted silently, with the last occurrence
  winning. It is now an error at the call site.
- **An unterminated `${` token now fails loudly.** A first-pass value containing an
  opening `${` with no closing brace was previously passed through as raw literal
  text; it is now a parse error. There is no literal-brace escape yet, so a value
  that legitimately contained the characters `${` now fails &mdash; rewrite it to
  avoid the sequence until an escape mechanism lands.

### Schema and validation

- **`allowed_values` is now enforced under repeated containers.** A constrained
  value nested inside a repeated container was previously left unvalidated; its
  allowed set is now checked on every instance.
- **`unique` is now enforced under repeated containers.** A uniqueness constraint
  nested inside a repeated container was previously unenforced; duplicates across
  instances are now rejected.
- **Re-declaring an element on schema attach is now rejected.** A second
  declaration of the same element was previously absorbed silently. It is now an
  error at schema-build time, so a schema build that used to be clean can now fail
  &mdash; remove the redundant declaration.
- **Float parsing is now locale-independent and rejects hexadecimal.** On the
  Apple libc++ path, a floating-point value was parsed through the active C
  locale and would silently accept a hexadecimal float. Parsing is now fixed to
  the C locale and rejects hexadecimal input, so a value that used to misparse
  under a non-C locale now parses consistently or fails loudly.
- **An empty constraint-group surface is now gated.** A constraint group declared
  with no members was previously accepted as a no-op; it is now rejected at
  declaration.
- **A value-conditioned rule now resolves the correct instance.** Under a repeated
  container, a rule keyed on another value's value previously matched against the
  wrong instance silently. It now performs an instance-indexed lookup and matches
  the same instance.
- **A digit or bracket command-line delimiter is now rejected.** A `cli_delimiter`
  set to a digit or a bracket character was previously accepted and would corrupt
  parsing; such a delimiter is now rejected at configuration time.
- **An owner token now requires a `noexcept`-comparable payload.** This is a
  compile-time break: a payload whose comparison operator can throw no longer
  satisfies the owner-token contract. Mark the comparator `noexcept`.

### Source loading, XML fidelity, and emit

- **The emitter now reports errors through a return channel.** The emit entry
  point returned `void` and could not signal failure; its signature is now
  `expected<void, error>`. This is an API break: callers must inspect the
  returned result. Emit failures that were previously invisible are now surfaced.
- **Malformed and injection-shaped source input now fails loudly.** Several paths
  that silently accepted or absorbed malformed input &mdash; including a newline
  embedded in a flat key &mdash; now report a malformed-source error instead of
  producing a quietly wrong tree.
- **Silent value loss on XML round-trip is closed.** A root-anchored leaf,
  text carried directly on a repeated container, and a whitespace-only value or
  CDATA section on a repeated container were previously dropped on the way out
  and back; all now round-trip intact.
- **A whitespace-only value now round-trips instead of collapsing to empty.** Such
  a value is now emitted inside a CDATA section, so it reloads as the original
  whitespace rather than as an empty string.

### Resolution

- **An unaddressable node now surfaces a diagnostic.** A resolution that could not
  address a target previously returned silently; it now reports the condition.
- **A malformed key path is now an error.** A key path that could not be parsed
  was previously ignored silently; it now fails with a diagnostic.
- **A precedence inversion is corrected.** Two sources at the same layer could
  resolve in the wrong precedence order; the ordering is now correct. Unlike the
  other entries here this one changes resolved values *silently* &mdash; no
  diagnostic is emitted &mdash; so re-verify the resolved output of any
  configuration that layers two sources at the same rank.
- **A same-rank duplicate under a uniting merge is now handled loudly.** Merging
  two same-rank entries under the `unite` mode previously resolved silently; the
  collision is now surfaced.
- **The inheritance-admissibility exemption on the security seam is tightened.** A
  path that could bypass the admissibility check on the inheritance seam is
  narrowed, closing a silent exemption.
- **The flat-grouping invariant is now enforced.** A grouping violation that was
  previously tolerated is now rejected.

### Configuration facade and builder

- **A malformed keyspace path is now rejected at both seams.** An anchor whose
  keyspace path was malformed was previously re-anchored silently at the root.
  It is now rejected loudly at registration *and* at the schema attach seam.
- **A spent builder now throws instead of yielding a divergent space.** Calling
  `name()` after `build()`, or calling `build()` a second time, previously
  produced a silently divergent sealed space. Both now throw.

### Documented XML round-trip behaviors

These two are not silent-to-loud conversions; they are lossless, idempotent, and
deliberate, and are recorded so they are not mistaken for defects.

- An empty unnamed configuration round-trips to a single empty configuration
  entry. The result is idempotent on a second pass.
- A multi-root unnamed configuration gains a configuration keyspace prefix from
  the emit wrapper. The result is idempotent on a second pass.
