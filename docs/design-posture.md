# Design posture

This document states the library's *declared position*: the principles and
boundaries behind the API &mdash; what the core decides versus what it leaves to
the host. The scope decisions below are intentional posture, not gaps waiting to
be filled. Reading them as posture is the point: an adopter who understands the
boundary composes with it instead of expecting the core to cross it.

## The core resolves on the host; constrained targets consume resolved values

nucleus is a host-side configuration engine. It folds a source stack against a
sealed schema, expands `${...}` tokens, converts typed values, and yields one
immutable, thread-readable `config`. All of that runs where the host runs &mdash; a
capable machine with a filesystem, an environment, and a command line.

A constrained target &mdash; a small on-device controller with no room for the
schema fold, the tokenizer pipeline, or the source machinery &mdash; is not where
resolution happens. Such a target consumes an **already-resolved** value: a byte,
a number, a short string the host resolved and handed down. nucleus deliberately
draws its boundary at that handoff. The engine's job is to turn many partial,
overlapping, human-authored inputs into one settled answer on the host; delivering
that answer to a constrained consumer, in whatever transport and encoding that
consumer expects, is the host's job. The core neither shrinks itself onto the
device nor pretends the device runs the fold.

## Static commissioning, not live reconfiguration

nucleus resolves a configuration once, from on-disk documents and the other
sources in the stack, into a sealed `config`. That is a *commissioning* act:
the inputs are settled documents read at load, and the result is immutable for
its lifetime. A host loads, reads, and &mdash; when inputs change &mdash; loads again
against the same sealed `config_space`, which is built to be reused across many
loads.

What the core does **not** do, by design, is watch a document for edits, hot-swap
a running `config`, or expose a live parameter that mutates after load. Dynamic
reconfiguration and live reload are the host's domain: a host that needs them
owns the file-watching, the reload trigger, and the swap of one immutable `config`
for its freshly-loaded successor. The immutability of a resolved `config` is a
feature &mdash; it is what makes the result freely thread-readable without a lock &mdash;
and live mutation would trade that away. Adopters should read the absence of a
reload API as this posture, not as a missing feature.

## Defaults as the bottom layer

Source-stack order is precedence: a later-listed source overlays an earlier one,
and documents occupy the lowest ranks beneath the stack's sources. The idiom that
falls out of this is **defaults-as-bottom-layer** &mdash; place a source at the lowest
rank purely to supply baseline values, and let every higher layer override them.

A programmatic source is the natural carrier for such defaults: it is populated in
code, needs no file, and sits wherever the host puts it in the stack. Put it first
and it becomes the floor.

```cpp
nucleus::runtime_source defaults;            // lowest precedence -- the floor
defaults.set("server/host", "localhost").set("server/port", "8080");

nucleus::env_source env;                     // overrides the defaults
env.set("server/host", "staging-host");

auto loaded = nucleus::load_config(space,
    nucleus::source_stack{std::move(defaults), std::move(env)}, {});
// server/host resolves to "staging-host"; server/port stays "8080".
```

Every leaf the higher layers leave untouched keeps its default; every leaf they do
set wins. There is no separate "default" concept in the schema &mdash; a default is
just a value at the bottom of the precedence order, which is exactly what the fold
already understands.

## Secret injection is a tokenizer extension point, not a core feature

The core ships generic `${...}` tokenizers (environment, string) and installs them
on every builder. Host-specific vocabulary is the host's to build with
`tokenizer_builder` and inject through `install_tokenizer()` before `build()`.

Secret material follows that seam. A `${secret.*}` vocabulary &mdash; for example
`${secret.database_password}` resolving from a vault, a keyring, or a mounted file
&mdash; is a **host-built tokenizer**, not a shipped core feature. The host authors a
tokenizer under a `secret` category whose resolver reaches into whatever secret
store it trusts, installs it before sealing the space, and from then on a value of
`${secret.<name>}` expands at load exactly like any other token.

```cpp
nucleus::tokenizer_builder builder("secret");
builder.set_wildcard([&store](std::string_view name) -> nucleus::token_result {
    return store.fetch(name);            // the host's secret store, the host's trust decision
});
engine.install_tokenizer(std::move(builder).build());

// A value "${secret.database_password}" now resolves from the host's store at load.
```

The core deliberately neither stores nor resolves secrets. It does not know a
vault from a plain string, and it keeps no secret-aware code path. That is the
posture: the `${...}` seam is the extension point, secret handling is a policy the
host owns, and pushing secret storage into the core would put trust decisions in
the wrong place. What the core guarantees is only the mechanism &mdash; a named
tokenizer category, expanded at load to a fixpoint, with the same depth and cycle
guards as every other token.
