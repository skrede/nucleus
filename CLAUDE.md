Do not add Co-Authored-By lines to commit messages.

Do not be sycophantic nor agreeable to be appealing to the user.
The user values rigor, honesty and objectivity -- not a sycophancy.

Use american English: e.g., "stabilizing" not "stabilising", and "color" not "colour"

Never refer to issues, IDs and keys that are result of planning tools, project management tools, or issue trackers
(including phase numbers, milestones, plans or task numbers or any other kind of planning artifact ID/key -- including from GSD and .planning artifacts)
in any area of the code/bodebase, including but not limited to commit messages, code, code comments, documentation, and examples.

Never do git tagging, never merge and no force flags to circumvent .gitignore.

Commit message format:
{Prefix}: {summary sentence}.

- {what was done, one line per item}
- {another item if applicable}

Allowed prefixes: Feature, Fix, Refactor, Docs, Examples, Optimization, WIP
The summary line should be brief and descriptive. The bullet list expands on what was done.
Single-item commits may omit the bullet list.

There should be one commit per GSD plan within each GSD phase (so a phase with 3 plans has at least 3 commits);
you can make additional commits if you need them for safekeeping during development, but you should by default attempt
to make one commit per plan (not GSD task or other things). Use the WIP prefix if the code in the commit does not
compile.

No issue tags, phase numbers, or planning tool references in commit messages, code, comments, docs, or examples.

Branching model: master (releases) ? develop (integration) ? milestone/<version> (work).
Create milestone branches from the current branch, named milestone/<version> (e.g., milestone/v0.2.0).
If develop does not exist, create it from master.
Merge path: milestone ? develop ? master. Never delete develop.
You can make commits to the milestone branch but never push.

.planning/ files should not be committed to the code project repository.
Do not override .gitignore to attempt to add files or folders, including .planning/.
./planning should have a separate shadow repository initialized (without submodules and independent of the code project)
where the state is kept.
If there is no such repo, leave the files to be locally.

Generate idiomatic, cross-platform C++20 code -- the code must run on macOS, Linux and Windows. Leverage the language features as much as possible!

Adhere to typical conventions of popular and modern C++ libraries, e.g., asio, boost, and so on;
existing conventions in the project take precedence, if you are unsure about conventions, or existing conventions
contradict typical and popular conventions, ask the user what to do.

Use header guards and not pragma once. Header guards are on the format HPP_GUARD_<NAMESPACE>_<FOLDER>_FILENAME_H.
Do not add matching comment // namespace "the namespace" after closing namespace brackets, e.g., } //
namespace {namespace name}
Do not add matching comment // HPP_GUARD_... define macro after the include guard #endif

The #include order for the project is as follows (unless order matter for other reasons like something must come before
something else):

- internal project includes (#include with ") come at the top, third-party libs come second, and standard library
  headers come third.
- these three major "sections" are divided by a new blank line. Within the major sections, includes are grouped by
  folder location to form intermediate sections, which are separated by a blank line.
- Only one blank line between sections, even if a new major starts
- For each "section" of includes, all includes are sorted first by length, then with the same length alphabetically.

## Project context

nucleus is a standalone, domain-neutral C++20 configuration engine that unifies
schema, configuration, tokenization, and argument parsing behind one hierarchical
keyspace (`/`-separated FQN-style key paths). A registered schema is the single
upstream authority that dictates both the command-line surface and the document
structure simultaneously. It carries zero coupling to any embedding application
and must be useful to any C++ program.

Load-bearing architecture (do not violate without discussion):

- **Mechanism in core, policy in the host/adapter.** The core never decides
  ownership, reservation, or filename conventions. It exposes the hooks a host
  needs: identity-tagged registration (an opaque owner token the core stores and
  surfaces but never interprets), referential integrity, conflict/provenance
  reporting, and a registration-policy seam.
- **Flat registry ownership.** The schema, tokenizer, and source registries are
  flat sibling members of the top-level facade; none owns another. They
  collaborate by hand-off — passed each other via a transient resolution context
  at call time. Invariant: no registry stores a member reference to another
  registry; cross-registry needs are parameters.
- **The seam is `source`/`provider`, not "document parser".** A source yields
  keyspace entries `(path -> value + capability flags)`. Document sources
  (XML/...) are the common subcategory sharing a view-node model; argv and env are
  also sources. Feature availability = schema requirements intersected with source
  capabilities (graceful degradation, never silent data loss).
- **Two-phase lifecycle.** A `configuration_space` facade is `configurable`
  (register_schema/register_tokenizer/register_source) until `load()`/`resolve()`,
  which yields an immutable, self-owning, freely-thread-readable `configuration`.
  Registration after resolve is an error.
- **View-node memory model.** Values are view-or-owned (`string_view` into a
  retained buffer with an optional ownership handle). Buffers (including all
  transitively inheritance-referenced files) live through parse + resolve; values
  are copied out at the resolve boundary, then buffers are dropped.
- **Seams ship first.** Even where only one implementation exists, the boundary is
  real — never hardcode a single format and "generalize later."

Conventions and naming:

- The configurable facade type is `configuration_space`; the resolved immutable type is
  `configuration`.
- Schema registrations anchor via a typed API (`anchor::root` /
  `anchor::keyspace("name")`); anchors are code/schema-side only and never appear
  in document text.
- Token syntax is `${...}`. Generic tokenizers (env, uuid, file/dir/self, string,
  scope) live in core; a `HOST` tokenizer is an opt-in built-in module. Logging is
  a `log_sink` seam (level + message, `std::format` only, no-op by default) with
  no dependency on any logging library — the host injects the bridge.
- Error handling uses an in-house `result<T, E>` in public headers (not
  `std::expected`, which is C++23 and outside the C++20 contract). `std::format`
  is the diagnostic vocabulary, with an `fmt` fallback gated on `__cpp_lib_format`.
- XML support wraps pugixml in its own source module (privately linked, never
  reachable from core); do not recreate or displace it.
- License: Apache-2.0.