# Changelog

All notable changes to `nucleus` are recorded here. This file is written for the
upgrader: each entry names a change you would not otherwise predict, and why it was
made. A change may turn a previously clean load, build or emit into a failure,
change a resolved value or emitted text without a diagnostic, change an error code,
change what building or installing nucleus does, or stop code that used an API from
compiling; each entry says which. From 0.4.2 on, each release also lists its
additions. Entries describe behavior, not internals.

## 0.4.2 (unreleased)

This release makes the emitters honest. Where a previous version would render
something other than what was resolved &mdash; an ambiguous record, an invalid XML
name, a collapsed instance ordinal &mdash; the emitter now reports before producing
any output. Most entries from Instance ordinals through Delivery turn a previously
clean emit, or the load before it, into a failure; the rest change the emitted text,
the API or an error code, and each says which.

It reaches well beyond emission. Registration refuses more than it did: names,
schema paths and allowed values carrying a control character or shell syntax, empty
host callables, and malformed identity groups. Several APIs moved from throwing, or
from returning a bare value, to the value channel, so code that used their results
directly no longer compiles; the builder seal among them reverses a 0.4.1 entry, as
does the tokenizer's single substitution budget. A number of loads that succeeded
before now fail. Some collection and read fixes instead change resolved values
*silently* &mdash; no diagnostic is emitted &mdash; and each is called out as such:
re-verify the output of any configuration with repeated or keyed containers.
Generated bash completion now requires bash 5.0, and what nucleus requires to build
and what an installed package hands a consumer both changed. Read every group that
matches what you use. What the release adds without changing an existing interface
is listed last, under Additions.

### Instance ordinals

- **An instance ordinal above 4294967295 is now rejected.** The bracket grammar
  previously accepted up to 18 digits, but the engine represented an ordinal in
  `std::size_t`, so on a 32-bit build every ordinal past `UINT32_MAX` saturated to
  the same value: two distinct instances compared equal, matched the same anchor,
  and rendered identically. The accepted domain is now 1 to 10 decimal digits with
  a value no greater than 4294967295 &mdash; what every supported platform holds
  losslessly &mdash; and anything larger fails as a malformed key path naming the
  source and the path.
- **`key_path::ordinal_of` now returns `std::uint64_t`.** It returned `std::size_t`,
  and `ordinal_sort_key` pairs carry `std::uint64_t` the same way. Assigning the
  result to a `std::size_t` narrows only where `std::size_t` is
  narrower than 64 bits, as on a 32-bit build; code that deduced the type with
  `auto` now holds `std::uint64_t` on every platform.

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
- **A repeated instance now renders with its ordinal.** A document carrying
  `cluster/node[0]/port` renders as `--cluster-node-0-port=80` under argv and as
  `cluster/node[0]/port=80` under env. 0.4.1 stripped every ordinal and wrote
  `--cluster-node-port=80` once per value, leaving the read order to rebuild the
  instances. The text of every emitted document with a repeated container changes;
  `argv_source` and the env source read the new form back to the same instances.
- **An emit anchor is now matched against concrete instances.** Anchoring at a
  repeated container renders each instance led by its ordinal (`--0-port=80`), and
  anchoring at one instance (`cluster/node[1]`) renders that instance alone. 0.4.1
  compared the anchor as a plain text prefix and silently skipped every key beneath
  a repeated anchor. An anchor that names a scalar key, which 0.4.1 also skipped, is
  now refused with `errc::malformed_source`.
- **argv rendering refuses a key segment that carries the delimiter.** Under the
  default `-` delimiter, a key such as `app/log-level` is refused with
  `errc::malformed_source` in both the template and the document, because
  `--app-log-level` reads back as `app/log/level`. 0.4.1 wrote it. A schema with a
  hyphenated element name needs a different delimiter to be emitted as argv.

### XML emission

- **Element names and text are validated against XML 1.0 before anything is
  emitted.** Malformed UTF-8, a name that is not a valid `NameStartChar`/`NameChar`
  sequence, and code points outside `Char` (including U+FFFE and U+FFFF) previously
  produced a success result and an unparseable document. They now report.
- **XML template rendering now validates at all.** Template emission previously
  checked nothing, including the wrapper element, despite the document path
  validating it. A schema element whose name carries bracket-index notation is now
  refused at registration rather than emitted as the invalid name `<node[0] />`.
- **The XML document emitter now takes the sealed space, and its old overloads are
  gone.** `xml::emit_document(config, out, space_name)` and the overload taking a
  `schema_projection` are removed. `xml::emit_document(config, space, out,
  space_name)` is the ordinary, schema-aware form, and
  `xml::emit_document_schema_blind(config, out, space_name)` is the explicit
  config-only one. `xml::emitter` is now a class constructed from the space name,
  with no public `space_name` or `proj` member, and its `emit_document` takes the
  space as well. `xml_emitter.h` no longer includes `config.h`, `config_space.h` or
  `schema/projection.h`. Each of these is a compile error, not a behavior change.
- **The schema-aware XML document form refuses a configuration the space does not
  describe.** Before any output it reports, with `errc::malformed_source`, a path
  the schema does not declare, an ordinal on a path that is not repeated, a
  repeated path without one, a value on a structural path, and a required primary
  key that is absent from an instance. Primary keys render as attributes on their
  container without a projection being passed in.

### Delivery

- **`unwritable_destination` is documented as the delivery failure it is.** An
  emitter renders and validates a complete document in owned storage before
  delivering it; a failure to accept those bytes is reported as
  `unwritable_destination`, distinct from the renderer's `malformed_source`. An
  emitter never flushes: success means the stream buffer accepted every byte, and a
  later flush, close or persistence failure is the caller's and lies outside the
  emitter result. A short write's accepted prefix is not rolled back.
- **The `config_emitter` concept now requires the owned rendering pair.** It
  requires `render_template(space)` and `render_document(config, space)`, each
  returning `expected<std::string, error>`, where it required
  `emit_template(space, out)` and `emit_document(config, out)`. A host emitter
  written against the 0.4.1 concept no longer satisfies it. The shipped emitters
  keep their stream-writing `emit_*` functions beside the new `render_*` ones.
- **Every `errc` value from `invalid_inheritance` on moved up by one.**
  `unwritable_destination` was inserted after `malformed_source`, and the
  enumeration carries no explicit values, so `static_cast<int>` of those codes, and
  `std::error_code::value()` for them, differ from 0.4.1. Nothing reports the
  change: a code stored, logged or compared as a number under 0.4.1 now names a
  different failure. Compare enumerators, never their integer values.

### Shell completion

- **The generated bash completion now requires bash 5.0.** On bash 3.2, the macOS
  `/bin/bash`, and on any 4.x, including the 4.4 of RHEL 8 and its rebuilds, the
  script still sources cleanly but registers nothing, and says nothing: the source
  exits 0 and `complete -p` is simply empty. A bash script loaded into zsh through
  `bashcompinit` registers nothing either, since zsh has no `BASH_VERSINFO`; load
  the zsh script there.
- **The script's last line now guards the registration.** It reads
  `if [ "${BASH_VERSINFO[0]}" -ge 5 ]; then complete -F _<prog>_complete <prog>; fi`
  where it used to be a bare `complete -F` line. A host that rewrote that line
  &mdash; to append aliases, or to strip it and register further names &mdash;
  must adjust to the new shape.
- **A program name that is not a bare command token is refused.**
  `config_space::generate_completion` now returns `expected<std::string, error>`
  rather than `std::string`, so a caller that used the result as a string no
  longer compiles. The name reaches shell command position unquoted, so a `prog`
  that opens with anything but a letter, a digit or `_`, or that carries anything
  but letters, digits, `.`, `_` and `-`, is refused with `errc::malformed_source`
  before any script text exists. Each segment of a non-empty `space_name` is held
  to the same grammar, since it lands in the script's word list beside every flag.
- **A completion anchor with an ordinal on a non-repeated segment is refused.**
  `generate_completion` reports it with `errc::malformed_source`. A concrete anchor
  on a repeated container, such as `cluster/node[1]`, now completes the flags
  beneath it.
- **`completion_option::has_ordinal_wildcard` is replaced by `ordinal_slots`.** A
  host that builds or reads a `completion_model` itself no longer compiles. The new
  member holds the byte offset of each `*` in the flag that stands for an instance
  ordinal; a list that is not strictly ascending, or whose offsets do not land on a
  `*`, is ignored and the option completes as its literal flag. A flag crossing two
  repeated containers carries two slots, as in `--cluster-node-*-route-*-port`.
- **The text of the generated scripts changed throughout this release.** Beyond the
  last line, the bash and zsh scripts differ from 0.4.1's in many places. Treat
  them as opaque output to install as written; a host that post-processes either
  script must re-check its edits against the new text.

### Builder and lookup results

- **`config_space_builder::build()` now returns `expected<config_space, error>`.**
  A caller that used the result as a `config_space` no longer compiles. A second
  `build()` is reported as `errc::sealed_builder` and does not throw, and a
  `name()` on a spent builder no longer throws either: it still returns the
  builder, and the refused name is recorded as a report in `conflicts()`. This
  reverses 0.4.1's "A spent builder now throws" entry; both cases are now loud on
  the value channel instead.
- **`multispace_argv_source::for_space()` now returns
  `expected<space_view, error>`.** A name that was never registered is reported as
  `errc::malformed_source` naming the space, where 0.4.1 threw
  `std::invalid_argument`. A caller that used the result as a `space_view` no longer
  compiles.
- **A `config` is now built from a raw map through `config::from_values`.** The two
  public constructors taking a value map are private, so code that constructed a
  `config` directly no longer compiles. `config::from_values(values, origins)`
  returns `expected<config, error>`, refusing a key that does not parse or that
  carries `=`, a carriage return or a line feed with `errc::malformed_source`, and a
  leaf stored both plain and indexed with `errc::schema_violation`.

### Registration

- **An element name must be one clean path segment.** `register_element` now
  refuses, with `errc::rejected_registration`, an empty name and one carrying a
  separator, a space, a control character &mdash; C0, DEL, the C1 range and the
  Unicode `Bidi_Control` set &mdash; or any of ``!"#$%&'()*:;<>?[\]`{|}~``. A name
  reaches the completion word list, which the shell expands rather than merely
  splits, and every shell draws it exactly as typed. 0.4.1 refused only a name that
  opens with a digit, which is still refused; `=` is still admitted.
- **A schema path segment is held to the element-name rule.** `register_schema`
  refuses, with `errc::rejected_registration`, a path that does not parse and one
  whose segment opens with a digit or carries a separator, a space, a shell
  metacharacter or a control character. Nothing of a refused path is stored.
- **An allowed value carrying a control character is refused.** Any control
  character in an allowed value, a newline or a NUL included, is refused at
  registration with `errc::rejected_registration`, naming the element and the
  value. 0.4.1 accepted it, and the completion menus then dropped the value without
  a word.
- **A child declared beneath a required element is refused.** The refusal names
  both elements. 0.4.1 accepted the declaration. A required leaf still attaches
  whether or not it repeats.
- **An empty host callable is refused when it is handed over.** Installing a
  tokenizer whose field or function resolver is empty, a tree tokenizer whose
  resolver is empty, or a converter that is empty is refused with
  `errc::rejected_registration`, naming what was empty. 0.4.1 accepted each and
  failed with `std::bad_function_call` the first time a load called it. A
  tokenizer's wildcard resolver may still be empty: that is how "no wildcard" is
  spelled.
- **An identity group may not reuse a namespace or list a member twice.**
  Registering a second identity group under a namespace name already held, or one
  naming the same member twice, is refused with `errc::rejected_registration`.
- **A CLI delimiter that opens with a digit or carries a control character is
  refused.** `cli_delimiter::parse` rejects `1x` as it already rejected `1`,
  narrowing 0.4.1's digit rule further, along with any C0 byte, DEL, C1 control or
  `Bidi_Control` code point. The parse reason quotes the delimiter escaped.

### Collections

These change resolved values *silently* &mdash; no diagnostic is emitted.

- **A layer now replaces only the repeated-container instances it addresses.** A
  two-instance layer over a three-instance base resolves to three instances: the
  first two from the higher layer, whole, and the third from the base. 0.4.1
  removed the third. Under nested repetition the unit replaced is the innermost
  instance the path names, and the rule holds inside a selected strain. A repeated
  leaf outside any repeated container is still replaced as one whole list.
- **Repeated leaves inside one instance each keep an ordinal of their own.** Two
  `<mark>` elements inside one `<node>` resolve to `mark[0]` and `mark[1]`. 0.4.1
  collapsed them onto one value, the last.
- **Repeated and keyed elements directly under a named space's root are now
  instances.** Under an XML source given a space name, sibling repeated elements
  beneath the root resolve to distinct indexed paths, and sibling keyed elements
  carry their key and reach strain selection. 0.4.1 collapsed them onto one path,
  the last sibling winning. A duplicate primary-key value there now fails the pull
  with `errc::malformed_source`.
- **Re-opening the selected strain across inheritance no longer drops its values.**
  Under `extend="wide"`, the instances of the parent's collection that the derived
  document does not address now survive; 0.4.1 dropped them. Under
  `extend="narrow"`, 0.4.1 dropped the strain's primary-key value, and every value
  of the strain once the derived document supplied an instance; the identity and
  the surviving instances now resolve, renumbered from 0 in their original order.
  A layer addressing one field of one instance in the selected strain also no
  longer drops fields of the instances it does not address.
- **A `replace_by_key` merge keeps each identifier at its first position.** An
  instance replaced by a higher layer stays at the ordinal where its identifier
  first appeared; 0.4.1 moved it after the lower layer's surviving instances.
- **A command-line flag given twice for one instance now takes the later value.**
  For a flag addressing an instance by ordinal, such as `--cluster-node-0-port`,
  0.4.1 kept the first occurrence on the command line. A flag addressing an
  instance nested inside another, such as `--c-n-0-r-0-p`, now loads; 0.4.1
  refused it as an undeclared path.

These can fail a load that succeeded before.

- **A key that names a repeated container without an instance fails the load.**
  With `node` declared repeated, a source supplying `cluster/node/port` now fails
  with `errc::malformed_source`, naming the source, the path and the container.
  0.4.1 stored it beside the instances, and only a typed leaf failed, at
  conversion.
- **A required element is checked in every instance.** A required element under a
  repeated container is reported missing, as `errc::schema_violation`, for each
  instance that lacks it, on that instance's own path. 0.4.1 was satisfied by any
  one populated sibling. A container resolving to no instances reports nothing.
- **A leaf stored both plain and indexed is refused.** A configuration carrying
  both `cluster/x/port` and `cluster/x[0]/port` fails the load with
  `errc::schema_violation`, naming both concrete paths.
- **A duplicate identifier within one layer of a `replace_by_key` merge is
  refused.** It fails the load with `errc::layering_violation`. 0.4.1 let the last
  instance in the layer win. The same identifier in different layers still
  composes.
- **A keyref resolves within its own instance and needs exactly one target.** A
  reference inside an instance of the namespace's repeated scope is matched only
  against that instance's identifiers; one outside every instance is matched
  against all of them. Zero or several candidates fail the load with
  `errc::schema_violation`, naming the qualified scope. 0.4.1 accepted a value
  found anywhere in the namespace. `follow_keyref` follows the same rule.

This one accepts more than before.

- **`unique` and identity-group values pool within their enclosing instance.** Two
  `<node>` instances may each hold a route on the same port, and two node instances
  may reuse one identifier. Duplicates inside one enclosing instance are still
  refused. This narrows 0.4.1's "duplicates across instances are now rejected" to
  the instance that encloses the collection.

### Reading a resolved configuration

The first three change results *silently*.

- **`get_all` and `get_all_as` honor explicit ordinals and order by every one.** A
  partly indexed query such as `cluster/node[1]/route/port` gathers the routes of
  `node[1]` alone, where 0.4.1 returned nothing, and nested results are ordered by
  the whole ordinal tuple rather than by the first ordinal alone.
- **Selector depth now counts observable edges, and `under()` is relative.**
  `at_depth(n)` counts the synthetic repeated node and each instance as an edge, and
  `at_depth(0)` selects the roots themselves, so on a root anchor 0.4.1's
  `at_depth(n)` is now `at_depth(n + 1)`; `children()` is `at_depth(1)`.
  `under(subpath)` now resolves relative to the selector's current roots, where
  0.4.1 matched an absolute path prefix; an omitted ordinal fans out across every
  instance, and the subtree it reaches becomes the root set for later structural
  operations.
- **`config_node::visit` now stops the whole walk when the callback returns
  false.** 0.4.1 skipped only that node's descendants and carried on with its
  siblings.
- **Typed reads on repeated paths report different codes.** `get_as` on a partly
  indexed path that still crosses a repeated container reports
  `errc::index_required` naming the first container it leaves unindexed, where
  0.4.1 reported `errc::absent_key`. `get_all_as` on a repeated path holding only
  untyped values reports `errc::missing_converter`, also where 0.4.1 reported
  `errc::absent_key`.

### Tokenizer

- **One substitution budget now spans both token passes.**
  `load_options::reference_budget` is gone, so code that set it no longer
  compiles. `load_options::expansion_budget` now bounds every substitution in the
  load, across the fold and the reference stage, and `0` selects the engine
  default of 10000. This reverses 0.4.1's 2500 first-pass cap and its "a value of
  `0` selects the 2500 default": a host that raised `expansion_budget` for the
  first pass now sets the ceiling for the whole load.
- **A tree tokenizer's output is resolved before it is stored.** A token carried in
  a resolver's output is now expanded, where 0.4.1 stored the output verbatim
  &mdash; a silent value change. A token body naming no scheme and no registered
  category fails the load with `errc::unresolved_token`, so a resolver returning
  `PREFIX=${HOME}/bin` must quote it as `PREFIX=${"${HOME}"}/bin`. A resolver that
  re-emits its own token fails as a cycle, and one depth cap bounds the nesting
  across the whole expansion.
- **A `}` inside a quoted fallback arm of a tree reference stays literal.**
  `${abs:a/missing ?? "}"}` now yields `}`; 0.4.1 ended the token at that brace
  and yielded `""}`. A quoted function argument already behaved this way.
- **A tree-tokenizer field naming a nested path is refused before dispatch.** A
  field carrying `/` is reported as a missing field, so a `??` fallback still
  applies, and the host resolver is never called with it.

### Inheritance and document loading

- **An empty `inherit` attribute is refused.** `inherit=""` fails the pull with
  `errc::malformed_source`. 0.4.1 read it as a relative parent path of `""`, joined
  it onto the declaring file's own directory, and handed that directory to the
  document factory as if it were a document.
- **Admissibility is decided before a candidate parent is read.** The policy's
  `admissibility` callback now runs once the factory has built the parent's handle
  and before the parent is pulled, so a refused parent is never parsed and its own
  ancestors are never opened. A refused parent that is also malformed now reports
  the refusal rather than its parse error. Any I/O the factory performs to build
  the handle has still happened.
- **Naming documents without a parser factory fails the load.** Non-empty
  `load_options::document_paths` with an empty `make_document` is reported as
  `errc::unreadable_source`. 0.4.1 skipped the whole inheritance expansion and
  loaded the rest of the source stack without the documents.

### Diagnostics

- **Host text quoted in a refusal or a diagnostic is now escaped.** Registration
  and completion refusals, tree-resolver diagnostics, the CLI delimiter's parse
  reason and the multispace lookup quote host-supplied names, paths and values
  through one escaper: every byte outside printable ASCII becomes `\xNN`, with
  `\n`, `\r` and `\t` for those three, and a backslash is doubled. Non-ASCII text,
  UTF-8 letters included, therefore reads as escapes. A host that matches on message
  text must re-check its patterns; several keyref and validation messages were also
  reworded.
- **Generated help prints a control character as an escape.** `generate_help`
  renders a control character in a name, a description, an allowed value or the
  program name as an escape such as `\x1b`, so the text cannot drive the terminal
  that shows it. Under a concrete repeated anchor it now lists the flags beneath
  the anchor.

### Schema API

- **`merge_mode::wholesale_replace` is now `merge_mode::replace_by_ordinal`.** The
  old enumerator is gone under every spelling &mdash; there is no alias and no
  deprecated form &mdash; so a translation unit that names it no longer compiles.
  The rename itself is a compile error, not a behavior change: the mode is still
  the default. What the mode does changed separately in this release &mdash; it
  now replaces, whole, only the instances a higher layer supplies; see the first
  Collections entry. Only code that spelled the default explicitly is affected; a
  schema that never named a merge mode needs no edit. The new name states the axis
  the replacement runs on, beside its sibling `replace_by_key`. Six Catch2 tags
  moved with it, from `[wholesale_replace]` to `[replace_by_ordinal]`, so a local
  script that filters tests on the old tag now selects nothing and says nothing.

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
- **A package built with `std::format` no longer compiles for a consumer whose
  standard library lacks it.** This is the one pairing that cannot work: the
  archive carries `std::format` in its interface, and `nucleus/format.h` now
  includes `<format>` because the package says so. Under 0.4.1 the consumer's own
  `__cpp_lib_format` test quietly selected `{fmt}` and compiled against a different
  backend than the archive's. Build the package with the consumer's standard
  library, or configure it with the `{fmt}` fallback.
- **pugixml 1.15 and `{fmt}` 11 are now discovery floors for the build.** The build
  asks `find_package` for those versions, so a distro-supplied pugixml or `{fmt}`
  below them is no longer linked: FetchContent clones the pinned release instead,
  which needs network access. A populated copy below the floor, and one whose
  version cannot be read, fail the configure.
- **`NUCLEUS_INSTALL` gates nucleus's install and export rules, and defaults off
  when nucleus is vendored.** A project that vendors nucleus and installs its own
  executables no longer has to keep a vendored dependency's install rules on just
  to satisfy an export set that dependency is not in. One shape still needs them
  on: if your own export set holds a static library that links nucleus, the private
  link survives in the exported interface and the identical export error moves one
  level up. Configure with `-DNUCLEUS_INSTALL=ON` in that case.
- **Configuring a top-level nucleus clone now sets its `core.hooksPath`.** When
  nucleus is the top-level project of a git clone, configure points that clone's
  `core.hooksPath` at `.githooks`, unless it already names another directory; the
  pre-commit hook there measures each staged unit against the size ceilings, and
  skips with a warning when `ctags` or `jq` is missing. Pass
  `-DNUCLEUS_INSTALL_GIT_HOOKS=OFF` to leave the clone's configuration alone. A
  vendored or fetched nucleus never touches it.

### Additions

- **Every shipped emitter can render into an owned string.** `nucleus::argv`,
  `nucleus::env` and `nucleus::xml` each add free `render_template` and
  `render_document` functions, taking the arguments of their `emit_*` counterparts
  without the stream, and each `emitter` type adds the members
  `render_template(space)` and `render_document(config, space)`. Each returns
  `expected<std::string, error>` holding the complete, validated document, so a host
  can inspect, store or deliver it itself. The XML module also adds
  `render_document_schema_blind(config, space_name)`.
- **`nucleus/utility/escaped_text.h` is a new public header.**
  `nucleus::escaped_text(text)` returns the text with every byte outside printable
  ASCII, and the backslash, written as an escape such as `\n` or `\x1b`: the form
  nucleus uses when a diagnostic quotes untrusted text, so a host can quote such
  text in its own messages the same way. `nucleus/utility/display_controls.h` is
  installed beside it and holds the classifier nucleus applies to host text it
  draws in help and completion output &mdash; C0 and C1 controls, DEL and the
  Unicode `Bidi_Control` set, recognized by UTF-8 sequence &mdash; but its functions
  live in `nucleus::detail` and remain implementation detail.
- **The read headers compile without RTTI.** `nucleus/config.h`,
  `nucleus/identity.h` and `nucleus/query/query.h` no longer use `typeid`, so a
  translation unit compiled with `-fno-rtti` can include them;
  `nucleus/config_node.h` already could. Under 0.4.1, `config::get_as` and the
  `owner_token` comparison used `typeid`, and `query.h` reached the latter.
- **A typed ordinal now completes the rest of a repeated-container flag.** In the
  generated bash completion, `--cluster-node-3-<TAB>` offers `--cluster-node-3-port`,
  a value after it completes as it does for the unnumbered flag, and a flag crossing
  two repeated containers takes an ordinal for each. 0.4.1 matched the typed word
  only against the literal `--cluster-node-*-port`, so once an ordinal was typed it
  offered nothing.

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
