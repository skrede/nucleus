# Descriptions and `--help` Text

A schema element can carry a short, human-readable description. That single field
is the one source both the shell completions and a projected `--help` text read
&mdash; declare it once and it flows to both, so they can never drift.

## Attaching a description

`described(element, text)` attaches a description to an element, mirroring the
`enum_element` / `merging` attach idiom &mdash; it takes an element and returns it
with the description set:

```cpp
builder.register_element(nucleus::described(
    nucleus::enum_element("level", nucleus::anchor::keyspace("logging"),
                          {"debug", "info", "warn", "error"}),
    "set the logging level"));
```

The description lives on the same element as its value set, so both are declared
together at the point the element is registered.

## Flow to shell completions

`config_space::generate_completion` projects the schema into a completion script.
The zsh emitter renders the description inline in each `_arguments` spec:

```
'--logging-level=[set the logging level]:value:(debug info warn error)'
```

bash completion carries no per-flag help text, so the description is simply not
surfaced there &mdash; the same script otherwise. Nothing about the description
changes the flag grammar; completion and the real CLI stay in lockstep through the
same `flag_of()` mapping.

## The `--help` projection

`config_space::generate_help(prog, delimiter, anchor)` returns plain `--help` text.
It reads the declared schema elements directly &mdash; not the completion model
&mdash; because a help line needs the `required` flag, which the completion model
does not carry. Each element becomes one line under its top-level keyspace group:

```
mytool options:

logging:
  --logging
  --logging-level  set the logging level [values: debug, info, warn, error]

server:
  --server
  --server-host  the address the server binds to (required)
```

Each line carries the flag, its description, its allowed-values list, and a
`(required)` marker &mdash; each part appended only when present. Lines are grouped
by the first path segment (the top-level keyspace), and the groups are emitted in a
stable order so the text is reproducible.

Only the returned string crosses the boundary: nucleus ships no `--help`
subcommand. The host owns how the text is surfaced &mdash; printed on `--help`,
folded into a larger usage banner, or written to a man page. Flags render under the
host-chosen `delimiter` and, when an `anchor` is given, relative to that subtree,
matching the argv grammar exactly.

## Single source of truth

Both projections read the same `description` field. There is no second place to
edit and no chance for the completion help and the `--help` text to disagree: a
change to the element's description moves both in lockstep.
