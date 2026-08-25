# Size-ceiling exceptions

The project instructions set a 200-line ceiling on a file and a 25-line ceiling on a function, and
say the only sanctioned over-limit units are the ones listed here. This is that list.

The ceilings are stated in the project instructions rather than in `CONVENTIONS.md`, which is
otherwise the authoritative style spec. That split is worth closing; until it is, the instructions
are the source for the two numbers used below.

Entries come in two kinds, and the distinction matters:

**Sanctioned** units are ones where the ceiling is deliberately waived, with a reason. Going over
is the right answer for them and no one should decompose them to make a number go down.

**Carried** units are simply over the ceiling and not yet decomposed. They are recorded here so
the debt is visible and countable rather than implied by silence. Being listed is not permission:
the carried tables are expected to shrink, and a unit joins one only because the work has not
happened yet.

Nothing is sanctioned today. Every entry below is carried.

## Regenerating this file

The tables are mechanical, and each command below reproduces exactly one of them, row for row and
in the order printed. Recount rather than trusting them.

Library files over the line ceiling:

```
find lib \( -name '*.h' -o -name '*.cpp' \) | sort \
  | xargs wc -l | awk '$2 != "total" && $1 > 200 { print $1, $2 }' \
  | sort -k1,1rn -k2,2 | awk '{ print $2, $1 }'
```

Test files over the line ceiling:

```
find tests \( -name '*.h' -o -name '*.cpp' \) | sort \
  | xargs wc -l | awk '$2 != "total" && $1 > 200 { print $1, $2 }' \
  | sort -k1,1rn -k2,2 | awk '{ print $2, $1 }'
```

CMake units over the line ceiling. Tracked files only, so a build tree or a fetched dependency
under the working directory cannot contribute rows:

```
git ls-files '*.cmake' '*CMakeLists.txt' | sort \
  | xargs wc -l | awk '$2 != "total" && $1 > 200 { print $1, $2 }' \
  | sort -k1,1rn -k2,2 | awk '{ print $2, $1 }'
```

Example files over the line ceiling:

```
find examples \( -name '*.h' -o -name '*.cpp' \) | sort \
  | xargs wc -l | awk '$2 != "total" && $1 > 200 { print $1, $2 }' \
  | sort -k1,1rn -k2,2 | awk '{ print $2, $1 }'
```

Library functions over the function ceiling, aggregated per file:

```
find lib \( -name '*.h' -o -name '*.cpp' \) | sort \
  | xargs ctags --output-format=json --fields=+ne --languages=C++ --c++-kinds=f -o - \
  | jq -s -r '[.[] | select(._type == "tag" and .kind == "function")
               | {f: .path, len: (.end - .line + 1)}]
              | map(select(.len > 25)) | group_by(.f)
              | map({f: .[0].f, n: length, longest: (map(.len) | max)})
              | sort_by(-.longest, .f) | .[] | "\(.f)\t\(.n)\t\(.longest)"'
```

`tests/cmake/check_local_size_growth.cmake` reads this file. A unit within both ceilings passes;
a unit over one passes only when a table below records a figure it does not exceed, and is refused
outright when no row names it. The gate uses the same tool and the same thresholds as the recipes
above, so a row's figure and a gate measurement of the same unit are the same number.

The gate reads rows by shape, not by section, and it skips everything between fence markers. The
recipes above therefore stay documentation however closely they come to resembling a table, and an
example row written to illustrate the format grants nothing to the unit it names. A row must sit
outside a fence to count, and no unit may appear twice under one metric: a duplicate is a fatal
error rather than a silent first-match win.

Adding a row is therefore the only way past a ceiling, which is to say it is also a bypass. It is
accepted as one because it cannot be done quietly: the row is a diff a reviewer reads, arguing for
the exception in the same change that takes it. Growing past a figure already recorded needs the
row edited, which is the same diff again.

The measurements below were taken with Universal Ctags 6.2.1 and jq 1.8.2 at the head of the
current working branch.

## Where the count stands

| | Over the line ceiling | Over the function ceiling |
|---|---|---|
| Before the resolution-context and schema-registry decomposition | 46 units | 62 functions in 25 files |
| Now | 49 units | 45 functions in 23 files |

Two units left the register: `resolution_context.h` fell from 1890 lines to 196, and
`schema_registry.h` from 534 to 178. The twenty-nine units split out of them are all inside both
ceilings, so none joined. The fifteen over-ceiling functions those two files held — ten and five —
are gone with them. The figure has since fallen by two more: `config.h` and
`configuration_space.cpp` each measure one fewer over-ceiling function than the register had
recorded.

Three units have since joined, all of them contract tests that rename the example entry point they
include. Declaring that renamed entry point is one line each, and each of the three was within a
line of the ceiling already.

A fourth has joined that is not a test. `examples/composition/plugin_spaces.cpp` crossed the ceiling
when its one packed 245-character definition was broken into the multi-line form the rest of the
file uses, and it is the first example the register carries.

The "before" figures needed a correction before they could be compared against. The earlier
tables covered C++ files only, while the gate has always measured CMake units by name as well;
the two CMake units over the ceiling were therefore absent from a register that claimed to be the
whole list. They are counted in the 46 above and listed below.

Nothing else about the baseline was wrong. Re-running the commands above against the tree as it
stood before the decomposition returned every C++ row the earlier tables recorded, values and per
file function counts included; the only discrepancy was the order of two test files tied at 202
lines, which the earlier recipe left to an unstable sort and the tie-break above now fixes. So the
missing CMake units were the sole correction the baseline needed.

## What the two trees outside `lib/` may register

Outside `lib/` the register holds two tables, both of line counts: one for test files and one for
example files. Neither tree has a function table, and the function table below covers library units
only. That asymmetry is deliberate rather than an oversight.

No function outside `lib/` may be registered. A function over the 25-line ceiling in `tests/` or in
`examples/` has exactly one remedy, which is to be decomposed; there is no row for it to take and
none may be added. That half is a ratchet and the ratchet is the point: because no function row can
be added, the gate refuses any change whose file listing contains a unit outside `lib/` with an
over-ceiling function, and the only way through is to bring that unit into compliance first.

The grant is to line counts only. An example file may be recorded over the 200-line ceiling when
splitting it would harm the file, and the reason is stated beside it: a worked walkthrough is read
top to bottom, and splitting one breaks the order the reader follows, which is the whole point of
the file. That is the kind of reason this accepts, and it is a reason a reviewer can disagree with
in the diff that takes the row. Packing lines wide to hold a file under its ceiling is not a remedy
and never was — it is what produced the collision between this register and the formatter.

The test-file table records line counts only. Those thirty-three units are carried debt awaiting a
reorganization of the tree, and that table is expected to shrink rather than grow; where it has
grown, the reason sits beside it.

The test tree carries one exemption, and only the test tree. A Catch2 test-case body — the block a
`TEST_CASE`, `SCENARIO`, `TEMPLATE_TEST_CASE` or `TEST_CASE_METHOD` macro introduces — is held to a
ceiling of its own instead of the 25-line function ceiling, which is a guideline for it rather than
a gate. A body over 60 lines is refused outright and nothing may excuse it: there is no
test-function table here and none may be added. Ordinary functions under `tests/` keep the 25-line
ceiling, again with no row available. The 200-line file ceiling is unchanged everywhere.

The exemption reaches neither of the other trees. In `lib/` the 25-line ceiling stays hard and a
register row remains the only escape; in `examples/` it stays hard with no escape at all, since the
example grant reaches line counts and nothing else.

It exists because ctags emits no function tag for a macro-introduced body, so until the gate was
taught to walk them by brace depth it had measured none of the lines holding nearly all the test
logic. Of the 840 bodies under `tests/` the median is 16 lines and the longest is 60, so 60 refuses
what no readable scenario needs while leaving setup-heavy integration cases room.

The test-case-body exemption moves no figure in "Where the count stands". Those count registered
units and the exemption adds no row, so the before-and-after numbers stand exactly as measured. The
example grant above is not in that position: it adds a row and moves the line-ceiling figure by one,
and the figure recorded there is a fresh measurement rather than the old one adjusted.

## Sanctioned

None.

## Carried — library files over the 200-line ceiling

13 of 160 units.

| File | Lines |
|---|---|
| `lib/core/src/nucleus/resolve/configuration_space.cpp` | 652 |
| `lib/xml/src/nucleus/xml/xml_source.cpp` | 630 |
| `lib/core/include/nucleus/detail/expected.h` | 543 |
| `lib/core/include/nucleus/config.h` | 495 |
| `lib/core/include/nucleus/schema/converters.h` | 387 |
| `lib/core/src/nucleus/tokenizer/token_lexer.cpp` | 335 |
| `lib/core/include/nucleus/config_space.h` | 296 |
| `lib/core/src/nucleus/tokenizer/resolver_scope.cpp` | 291 |
| `lib/core/src/nucleus/resolve/chain_walker.h` | 285 |
| `lib/core/src/nucleus/tokenizer/tree_resolver_scope.cpp` | 277 |
| `lib/core/src/nucleus/completion/completion.cpp` | 239 |
| `lib/argv/include/nucleus/argv/multispace_argv_source.h` | 232 |
| `lib/core/include/nucleus/schema/schema.h` | 217 |

## Carried — test files over the 200-line ceiling

33 of 157 units. Test units are held to the same ceiling.

Two rows here rose to close the pathname expansion the generated bash completion performed
over its own candidates, and one of the two rose again for the zsh description escaper.
`tests/completion_test.cpp` grew by the four emitted lines the pinned golden now carries, which
is the whole point of pinning it: a change to the emitted script that does not move the golden
is a change no reviewer sees. `tests/completion_smoke_test.cpp` grew by three cases that drive
the generated script under a real shell with the working directory seeded to match a candidate,
and then by one that pins the emitted `_arguments` spec construct by construct. The bash defect
was invisible to every text assertion that file already held — the emitted text was correct and
the shell rewrote it one stage later — so a shorter test there is one that proves nothing, and
compressing a case means dropping the seeding that is the case. The zsh case is long because it
is an enumeration: no zsh runs on the machine that wrote it, so each construct it claims to
neutralize has to be named and asserted rather than covered by driving a shell once.

The three newest rows are contract tests that rename the example's entry point through the
preprocessor so it cannot collide with the test runner's own. That rename leaves the renamed
function defined but never declared, which a missing-prototype diagnostic refuses. For two of the
three, the declaration that makes it legal is the single line that carried the unit past the
ceiling.

The third, `tests/example_plugin_spaces_contract_test.cpp`, was at 199 and the declaration alone
left it at exactly 200, needing no row. It takes one because of a second line: a blank that ends
an `AlignConsecutiveDeclarations` run so the declaration below it is not column-padded. That blank
is load-bearing across formatter versions rather than cosmetic. Measured on the pinned tool and on
the newer one the development host carries, the padded form this file held before is clean under
clang-format 18 and rejected by 22, while the form with the blank is accepted by both. A unit only
one version will leave alone is a unit that changes under whoever saves it next, which is the
local-and-CI divergence this register exists to keep visible. The line is the price of the file
being stable under both, and the row is the price of the line.

| `tests/inherit_chain_test.cpp` | 1118 |
| `tests/repeated_container_test.cpp` | 815 |
| `tests/typed_element_test.cpp` | 809 |
| `tests/config_node_test.cpp` | 777 |
| `tests/argv_source_test.cpp` | 615 |
| `tests/typed_shape_test.cpp` | 518 |
| `tests/repeated_element_test.cpp` | 470 |
| `tests/integration_shape_test.cpp` | 428 |
| `tests/strain_scope_policy_test.cpp` | 401 |
| `tests/keyed_projection_test.cpp` | 338 |
| `tests/schema_registry_test.cpp` | 333 |
| `tests/completion_smoke_test.cpp` | 327 |
| `tests/selector_test.cpp` | 315 |
| `tests/constraint_group_test.cpp` | 305 |
| `tests/load_front_door_test.cpp` | 289 |
| `tests/keyed_composition_test.cpp` | 286 |
| `tests/resolve/resolution_test.cpp` | 278 |
| `tests/completion_test.cpp` | 272 |
| `tests/schema_enforcer_test.cpp` | 272 |
| `tests/token_resolution_test.cpp` | 245 |
| `tests/discovery_test.cpp` | 240 |
| `tests/keyed_selection_test.cpp` | 232 |
| `tests/error/expected_test.cpp` | 227 |
| `tests/pkey_tokenizer_test.cpp` | 226 |
| `tests/source_stack_test.cpp` | 218 |
| `tests/system_multi_source_test.cpp` | 214 |
| `tests/collection_shapes/identity_pool_scope_test.cpp` | 208 |
| `tests/source_handle_test.cpp` | 204 |
| `tests/instance_addressing_test.cpp` | 202 |
| `tests/token_cycle_test.cpp` | 202 |
| `tests/example_constraint_groups_contract_test.cpp` | 201 |
| `tests/example_pkey_tokenizer_contract_test.cpp` | 201 |
| `tests/example_plugin_spaces_contract_test.cpp` | 201 |

## Carried — CMake units over the 200-line ceiling

2 of 29 units.

| File | Lines |
|---|---|
| `tests/CMakeLists.txt` | 721 |
| `lib/CMakeLists.txt` | 272 |

A CMake unit carries the line ceiling only. The gate measures function spans by running ctags
restricted to C++, which yields nothing for a CMake script, so there is no function column to
fill. That is the intended behavior and not a gap in the gate.

Decomposing `tests/CMakeLists.txt` is held back deliberately: it is entangled with a
reorganization of the test tree into folders, and doing one without the other would move the same
lines twice.

## Carried — example files over the 200-line ceiling

1 of 33 units.

An example is a worked walkthrough read top to bottom; splitting it breaks the order the reader
follows, which is the whole point of the file. The row below is granted on that reason and on
nothing else. `plugin_spaces.cpp` crossed the ceiling when the one packed 245-character definition
it carried was broken into the multi-line form the rest of the file uses, which is what both the
formatter and readability ask of it. The three lines are not bought back by packing something else,
and narrowing the example is not available either: the aggregate example gate asserts this
example's output, so a shorter walkthrough would mean editing the gate that checks it.

| File | Lines |
|---|---|
| `examples/composition/plugin_spaces.cpp` | 203 |

## Carried — library functions over the 25-line ceiling

45 functions across 23 files. Listed per file, since a file's largest function is the one that
sets the decomposition problem.

| File | Functions over ceiling | Longest |
|---|---|---|
| `lib/xml/src/nucleus/xml/xml_source.cpp` | 3 | 203 |
| `lib/core/src/nucleus/resolve/chain_walker.h` | 2 | 94 |
| `lib/argv/include/nucleus/argv/multispace_argv_source.h` | 1 | 86 |
| `lib/core/src/nucleus/tokenizer/resolver_scope.cpp` | 4 | 69 |
| `lib/core/src/nucleus/tokenizer/tree_resolver_scope.cpp` | 4 | 66 |
| `lib/core/include/nucleus/schema/converters.h` | 2 | 65 |
| `lib/core/src/nucleus/tokenizer/tokenizer.cpp` | 1 | 63 |
| `lib/core/src/nucleus/tokenizer/builtin_tokenizers.cpp` | 1 | 61 |
| `lib/core/src/nucleus/completion/completion.cpp` | 2 | 60 |
| `lib/core/include/nucleus/query/schema_query_context.h` | 2 | 58 |
| `lib/argv/include/nucleus/argv/cli_surface.h` | 1 | 57 |
| `lib/core/src/nucleus/tokenizer/token_lexer.cpp` | 5 | 57 |
| `lib/core/include/nucleus/config.h` | 1 | 52 |
| `lib/core/include/nucleus/completion/bash_emitter.h` | 1 | 51 |
| `lib/core/include/nucleus/config_source/feature_gate.h` | 2 | 47 |
| `lib/argv/include/nucleus/argv/argv_source.h` | 1 | 42 |
| `lib/core/src/nucleus/resolve/configuration_space.cpp` | 5 | 41 |
| `lib/core/include/nucleus/diagnostics/key_suggester.h` | 2 | 33 |
| `lib/core/src/nucleus/query/query.cpp` | 1 | 33 |
| `lib/core/include/nucleus/config_source/extension_registry.h` | 1 | 32 |
| `lib/core/src/nucleus/tokenizer/scope_keys.cpp` | 1 | 30 |
| `lib/core/include/nucleus/keyspace/keyspace.h` | 1 | 29 |
| `lib/core/src/nucleus/tokenizer/named_args.cpp` | 1 | 28 |

## Carried — construction-rule deviations

Not a size ceiling, but debt of the same kind and worth counting in the same place. The project
instructions say a member is initialized in its constructor's initializer list, never in its
declaration.

The table below is **not** a tree-wide count, and reading it as one would be a mistake. It records
exactly the members the resolution-context decomposition relocated verbatim rather than fixed, so
that the movement stayed reviewable as movement. One qualifies on that basis.

Others exist outside that scope and are not listed, counted, or waived here — `anchor::m_invalid`
in `lib/core/include/nucleus/schema/anchor.h`, `cli_flag::m_text` in
`lib/core/include/nucleus/schema/cli_flag.h`, `argv_source::m_policy` and `argv_source::m_log` in
`lib/argv/include/nucleus/argv/argv_source.h`, and `chain_walker::m_depth` in
`lib/core/src/nucleus/resolve/chain_walker.h`, among others. Every one sits in a class that does
have a constructor, so none is constrained the way `layered_handle` below is. A tree-wide recount
needs a parser rather than a grep and has not been attempted; until one is done, no total for the
tree should be quoted from this page.

| Member | Unit |
|---|---|
| `layered_handle::cached_batch` | `lib/core/src/nucleus/resolve/resolve_types.h` |

`layered_handle` is constrained. It is an aggregate, brace-initialized at its construction
site in `lib/core/src/nucleus/resolve/configuration_space.cpp`; giving it a constructor to hold
the initializer would make it a non-aggregate and break that site. Fixing it therefore means
changing the caller, which the decomposition deliberately avoided doing anywhere.

These are counted, not waived. `Sanctioned` above stays empty.
