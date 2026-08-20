# CLI Grammar and Multi-Space Addressing

## Single-space CLI

`argv_source` maps `--a-b-c=v` flags onto the same keyspace every other source
feeds. The default delimiter is `-`; any other choice passes through
`cli_delimiter::parse` before `argv_source::delimit_with()` can hold it. With
delimiter `_`:

```
--server_host=127.0.0.1  →  server/host = "127.0.0.1"
```

### Delimiter grammar

`cli_delimiter::parse` accepts a delimiter only when all of the following hold.
A rejected delimiter comes back as a reason string; it is never silently
repaired.

- It is nonempty.
- It does not contain `=` — the flag's key/value split would move.
- It contains neither a carriage return (CR) nor a line feed (LF) — a rendered
  record would end early. The flat renderers reject those same two bytes, and
  `=`, anywhere in a rendered key, and `config::from_values` rejects them in a
  submitted key.
- It does not contain `[` or `]`, which spell the ordinal index.
- It is not all ASCII digits, which would be indistinguishable from an ordinal
  segment.
- A multi-character delimiter must not contain `/`. The single-character `/`
  form is accepted, and is the one delimiter that is itself the keyspace
  separator: the flag body and the key path are then the same string.

Invertibility remains the host's contract: no schema segment may contain the
chosen delimiter as a substring, exactly as no segment may contain `-` under the
default.

The mapping is a pure bijection: the schema's `flag_of()` projects every
declared key back to exactly one flag, so `emit_template` / `emit_document` /
`generate_completion` and `argv_source` all share one grammar when given the
same delimiter. See the *trio invariant* in the main README.

## Ordinal segment rule — repeated containers

A repeated container's instances are addressed via a plain decimal ordinal
segment in the flag. The ordinal takes the place of the `[N]` bracket in the
FQN keyspace path:

```
--cluster-node-0-port=v  ⇄  cluster/node[0]/port
--cluster-node-1-port=v  ⇄  cluster/node[1]/port
```

**Accepted ordinal domain.** An instance ordinal is 1 to 10 decimal digits
whose value is no greater than `key_path::max_ordinal`, which is 4294967295. A
longer digit run, or a larger value, is rejected as a malformed key path: it is
never truncated and never wrapped, in either direction of the bijection. The
bound is not arbitrary. It is the largest ordinal every supported platform
represents losslessly in the type the read API hands back, so a key path that
parses on one target parses to the same number on all of them. The header
carries this bound as that named constant, and this document and the constant
state one figure.

**Disambiguation rule.** Schema element names must not start with a digit
(enforced at schema registration; XML element names already forbid this). A
digit-led segment that immediately follows a repeated container is therefore
unambiguously an ordinal index, making the bijection invertible.

**argv is override-only for collections.** argv can set or replace a field at
an in-range index; it cannot create new instances or extend a collection.
Supplying an out-of-range index is a loud pull error that names the actual
instance count. To populate a repeated container, use an XML source or
`runtime_source` (which carry the `duplicate_keys` capability needed to create
instances).

**Completion.** The completion script treats the index position as a digit
wildcard: after the user types `--cluster-node-2`, the sub-path completions
(`-port`, etc.) are offered for any digit value. Completion scripts have no
knowledge of the actual instance count at the time they are generated.

### Flat replay and repeated anchors

Rendered argv and environment artifacts are overlays over the same structural
base from which the configuration was loaded. They preserve concrete ordinals;
they do not append collection members or create an out-of-base instance. With
the default argv delimiter, the corresponding replay forms are:

```text
--cluster-node-0-port=8000  ⇄  cluster/node[0]/port = "8000"
--cluster-node-1-port=8001  ⇄  cluster/node[1]/port = "8001"
```

The argv renderer and source must use the same delimiter and anchor. A
canonical repeated anchor such as `cluster/node` selects every concrete
instance and retains its ordinal in the relative flag: `--0-port=8000`,
`--1-port=8001`. A concrete repeated anchor such as `cluster/node[1]` selects
only that instance and removes the fixed ordinal from the relative flag:
`--port=8001`. Environment emission has no anchor setting; it emits exact
concrete key paths such as `cluster/node[1]/port=8001`.

Replay equality is semantic: the exact concrete key set and stored strings are
preserved, with numeric ordinal ordering. Schema converters rerun and load
metadata such as provenance and degradations are recomputed rather than encoded
in the artifact.

## Multi-space CLI addressing

When a process hosts more than one independently-declared configuration space
(e.g. a core space and a plug-in space), `multispace_argv_source` partitions
one token vector across them by the flag's **first path segment**:

```
--alpha-x=1   →  alpha space,  key x = "1"
--beta-y=2    →  beta  space,  key y = "2"
```

### When to use `multispace_argv_source` vs plain `argv_source`

Use `argv_source` when the process owns a single logical configuration namespace
and every flag is unambiguously scoped by its path alone.

Use `multispace_argv_source` when flags from two or more independently-sealed
`config_space` objects share a single argv vector and need explicit
disambiguation. The first segment is the *space identity*: it is validated at
the source boundary and stripped before the key enters the keyspace, so each
space's schema operates on its own plain key paths.

### Grammar rule

Every flag submitted to a `multispace_argv_source` view MUST begin with the
registered space name as its first segment:

```
--<space_name><delimiter><rest>=<value>
```

Flags that match no registered space name are a loud pull error naming all
registered spaces. Flags addressed to a different registered space are silently
skipped by the current view (the other view picks them up on its own pull).
A bare flag with only the space name and no sub-segment (e.g. `--alpha`) is
also silently skipped (no inner key remains after stripping).

### Trio invariant extension for named spaces

The trio invariant — emit and completion produce exactly the flags that parse
correctly — extends to named spaces:

```cpp
argv::emit_template(space, out, delimiter, anchor, "alpha");
```
emits `--alpha-x=` for schema key `x` (with default delimiter `-`), which is
the exact token that `multispace_argv_source::for_space("alpha")` parses back.

```cpp
space.generate_completion(shell::bash, "mytool", delimiter, anchor, "alpha");
```
produces completion entries prefixed with `--alpha-`, matching the same grammar.

Both accept `space_name` as their last (defaulted) parameter so existing call
sites without a space name are unaffected.

### Setup example

```cpp
// Register two spaces.
nucleus::multispace_argv_source src(argv_tokens);
src.register_space("alpha").register_space("beta");

// Each view sees only its own flags.
auto alpha_view = src.for_space("alpha");
auto beta_view  = src.for_space("beta");

// Wire schema-coupled recognition.
alpha_view.recognize_with(nucleus::recognizer_of(alpha_space));
beta_view.recognize_with(nucleus::recognizer_of(beta_space));

nucleus::source_stack alpha_stack{nucleus::source_handle(alpha_view)};
nucleus::source_stack beta_stack{nucleus::source_handle(beta_view)};

auto alpha_cfg = nucleus::load_config(alpha_space, alpha_stack);
auto beta_cfg  = nucleus::load_config(beta_space,  beta_stack);
```

## Cross-format identity-envelope table

Every source format expresses space identity through a leading token that is
validated at the source boundary and stripped before the key enters the keyspace.
No ambiguity can arise because the identity check is the *first* operation of
each source's pull.

| Format | Identity token | Example |
|--------|----------------|---------|
| XML document | Root element name (`with_space_name("alpha")`) | `<alpha><x>1</x></alpha>` → key `x` |
| argv | First flag segment (`multispace_argv_source`) | `--alpha-x=1` → key `x` |
| Env prefix | Environment variable prefix (env_source prefix filter) | `ALPHA_X=1` → key `x` |

Symmetric emission follows the same principle: `xml::emit_template(space, out,
"alpha")` wraps the output in `<alpha>…</alpha>`, and
`xml::emit_document(cfg, space, out, "alpha")` wraps the emitted document in
the same envelope, so
`xml_source::with_space_name("alpha")` can parse the result directly.

## Canonical user-documentation paragraph

Hosts can adapt the following paragraph for their end-user documentation:

> This program accepts configuration flags prefixed with `--<space>-`, where
> `<space>` is the name of the configuration domain being addressed (e.g.
> `--server-host=127.0.0.1` for the `server` space). Flags not prefixed with a
> known domain name are rejected at startup with a message listing the valid
> domain names. You can also supply settings as XML (`<space>` as the root
> element), or as environment variables with an `<SPACE>_` prefix.
