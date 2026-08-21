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
| Now | 44 units | 47 functions in 23 files |

Two units left the register: `resolution_context.h` fell from 1890 lines to 197, and
`schema_registry.h` from 534 to 178. The twenty-eight units split out of them are all inside both
ceilings, so none joined. The fifteen over-ceiling functions those two files held — ten and five —
are gone with them.

The "before" figures needed a correction before they could be compared against. The earlier
tables covered C++ files only, while the gate has always measured CMake units by name as well;
the two CMake units over the ceiling were therefore absent from a register that claimed to be the
whole list. They are counted in the 46 above and listed below.

Nothing else about the baseline was wrong. Re-running the commands above against the tree as it
stood before the decomposition returned every C++ row the earlier tables recorded, values and per
file function counts included; the only discrepancy was the order of two test files tied at 202
lines, which the earlier recipe left to an unstable sort and the tie-break above now fixes. So the
missing CMake units were the sole correction the baseline needed.

## Sanctioned

None.

## Carried — library files over the 200-line ceiling

13 of 153 units.

| File | Lines |
|---|---|
| `lib/core/src/nucleus/resolve/configuration_space.cpp` | 656 |
| `lib/xml/src/nucleus/xml/xml_source.cpp` | 630 |
| `lib/core/include/nucleus/detail/expected.h` | 543 |
| `lib/core/include/nucleus/config.h` | 501 |
| `lib/core/include/nucleus/schema/converters.h` | 387 |
| `lib/core/src/nucleus/tokenizer/token_lexer.cpp` | 335 |
| `lib/core/src/nucleus/tokenizer/resolver_scope.cpp` | 305 |
| `lib/core/include/nucleus/config_space.h` | 296 |
| `lib/core/src/nucleus/resolve/chain_walker.h` | 285 |
| `lib/core/src/nucleus/tokenizer/tree_resolver_scope.cpp` | 277 |
| `lib/core/src/nucleus/completion/completion.cpp` | 240 |
| `lib/argv/include/nucleus/argv/multispace_argv_source.h` | 233 |
| `lib/core/include/nucleus/schema/schema.h` | 217 |

## Carried — test files over the 200-line ceiling

29 of 144 units. Test units are held to the same ceiling.

| File | Lines |
|---|---|
| `tests/inherit_chain_test.cpp` | 1118 |
| `tests/typed_element_test.cpp` | 822 |
| `tests/repeated_container_test.cpp` | 815 |
| `tests/config_node_test.cpp` | 777 |
| `tests/argv_source_test.cpp` | 615 |
| `tests/typed_shape_test.cpp` | 527 |
| `tests/repeated_element_test.cpp` | 470 |
| `tests/integration_shape_test.cpp` | 428 |
| `tests/strain_scope_policy_test.cpp` | 401 |
| `tests/keyed_projection_test.cpp` | 338 |
| `tests/schema_registry_test.cpp` | 333 |
| `tests/selector_test.cpp` | 315 |
| `tests/constraint_group_test.cpp` | 305 |
| `tests/load_front_door_test.cpp` | 289 |
| `tests/keyed_composition_test.cpp` | 286 |
| `tests/resolution_test.cpp` | 278 |
| `tests/schema_enforcer_test.cpp` | 272 |
| `tests/token_resolution_test.cpp` | 245 |
| `tests/discovery_test.cpp` | 240 |
| `tests/keyed_selection_test.cpp` | 232 |
| `tests/expected_test.cpp` | 227 |
| `tests/pkey_tokenizer_test.cpp` | 226 |
| `tests/completion_test.cpp` | 223 |
| `tests/source_stack_test.cpp` | 218 |
| `tests/system_multi_source_test.cpp` | 216 |
| `tests/collection_shapes/identity_pool_scope_test.cpp` | 208 |
| `tests/source_handle_test.cpp` | 204 |
| `tests/instance_addressing_test.cpp` | 202 |
| `tests/token_cycle_test.cpp` | 202 |

## Carried — CMake units over the 200-line ceiling

2 of 22 units.

| File | Lines |
|---|---|
| `tests/CMakeLists.txt` | 681 |
| `lib/CMakeLists.txt` | 236 |

A CMake unit carries the line ceiling only. The gate measures function spans by running ctags
restricted to C++, which yields nothing for a CMake script, so there is no function column to
fill. That is the intended behavior and not a gap in the gate.

Decomposing `tests/CMakeLists.txt` is held back deliberately: it is entangled with a
reorganization of the test tree into folders, and doing one without the other would move the same
lines twice.

## Carried — library functions over the 25-line ceiling

47 functions across 23 files. Listed per file, since a file's largest function is the one that
sets the decomposition problem.

| File | Functions over ceiling | Longest |
|---|---|---|
| `lib/xml/src/nucleus/xml/xml_source.cpp` | 3 | 203 |
| `lib/core/src/nucleus/resolve/chain_walker.h` | 2 | 94 |
| `lib/argv/include/nucleus/argv/multispace_argv_source.h` | 1 | 86 |
| `lib/core/src/nucleus/tokenizer/tree_resolver_scope.cpp` | 4 | 74 |
| `lib/core/src/nucleus/tokenizer/resolver_scope.cpp` | 4 | 69 |
| `lib/core/src/nucleus/completion/completion.cpp` | 2 | 66 |
| `lib/core/include/nucleus/schema/converters.h` | 2 | 65 |
| `lib/core/src/nucleus/tokenizer/tokenizer.cpp` | 1 | 63 |
| `lib/core/src/nucleus/tokenizer/builtin_tokenizers.cpp` | 1 | 61 |
| `lib/core/include/nucleus/query/schema_query_context.h` | 2 | 58 |
| `lib/argv/include/nucleus/argv/cli_surface.h` | 1 | 57 |
| `lib/core/src/nucleus/tokenizer/token_lexer.cpp` | 5 | 57 |
| `lib/core/include/nucleus/completion/bash_emitter.h` | 1 | 53 |
| `lib/core/include/nucleus/config.h` | 2 | 52 |
| `lib/core/include/nucleus/config_source/feature_gate.h` | 2 | 47 |
| `lib/argv/include/nucleus/argv/argv_source.h` | 1 | 42 |
| `lib/core/src/nucleus/resolve/configuration_space.cpp` | 6 | 42 |
| `lib/core/include/nucleus/diagnostics/key_suggester.h` | 2 | 33 |
| `lib/core/src/nucleus/query/query.cpp` | 1 | 33 |
| `lib/core/include/nucleus/config_source/extension_registry.h` | 1 | 32 |
| `lib/core/src/nucleus/tokenizer/scope_keys.cpp` | 1 | 30 |
| `lib/core/include/nucleus/keyspace/keyspace.h` | 1 | 29 |
| `lib/core/src/nucleus/tokenizer/named_args.cpp` | 1 | 28 |

## Carried — construction-rule deviations

Not a size ceiling, but debt of the same kind and worth counting in the same place. The project
instructions say a member is initialized in its constructor's initializer list, never in its
declaration. Three members still carry an in-declaration initializer. All three were relocated
verbatim during the resolution-context decomposition rather than fixed, so that the movement
stayed reviewable as movement.

| Member | Unit |
|---|---|
| `layered_handle::cached_batch` | `lib/core/src/nucleus/resolve/resolve_types.h` |
| `resolution_context::m_reference_budget` | `lib/core/src/nucleus/resolve/resolution_context.h` |
| `resolution_context::m_expansion_budget` | `lib/core/src/nucleus/resolve/resolution_context.h` |

`layered_handle` is the constrained one. It is an aggregate, brace-initialized at its construction
site in `lib/core/src/nucleus/resolve/configuration_space.cpp`; giving it a constructor to hold
the initializer would make it a non-aggregate and break that site. Fixing it therefore means
changing the caller, which the decomposition deliberately avoided doing anywhere. The other two
are unconstrained and simply not yet moved.

These are counted, not waived. `Sanctioned` above stays empty.
