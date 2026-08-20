# Size-ceiling exceptions

The project instructions set a 200-line ceiling on a file and a 25-line ceiling on a function, and
say the only sanctioned over-limit units are the ones listed here. This is that list.

The ceilings are stated in the project instructions rather than in `CONVENTIONS.md`, which is
otherwise the authoritative style spec. That split is worth closing; until it is, the instructions
are the source for the two numbers used below.

It has two parts, and the distinction matters:

**Sanctioned** units are ones where the ceiling is deliberately waived, with a reason. Going over
is the right answer for them and no one should decompose them to make a number go down.

**Carried** units are simply over the ceiling and not yet decomposed. They are recorded here so
the debt is visible and countable rather than implied by silence. Being listed is not permission:
the carried table is expected to shrink, and a unit joins it only because the work has not
happened yet.

Nothing is sanctioned today. Every entry below is carried.

## Regenerating this file

The tables are mechanical. Recount rather than trusting them:

```
find lib tests -name '*.h' -o -name '*.cpp' | xargs wc -l | awk '$1 > 200'

find lib -name '*.h' -o -name '*.cpp' | sort \
  | xargs ctags --output-format=json --fields=+ne --languages=C++ --c++-kinds=f -o - \
  | jq -s -r '[.[] | select(._type=="tag" and .kind=="function")
              | {f:.path, n:.name, len:(.end - .line + 1)}] | map(select(.len > 25))'
```

Counts below were taken at the head of the current working branch.

## Sanctioned

None.

## Carried — library files over the 200-line ceiling

15 of 123 units.

| File | Lines |
|---|---|
| `lib/core/src/nucleus/resolve/resolution_context.h` | 1890 |
| `lib/core/src/nucleus/resolve/configuration_space.cpp` | 656 |
| `lib/xml/src/nucleus/xml/xml_source.cpp` | 630 |
| `lib/core/include/nucleus/detail/expected.h` | 543 |
| `lib/core/src/nucleus/schema/schema_registry.h` | 534 |
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

## Carried — library functions over the 25-line ceiling

62 functions across 25 files. Listed per file, since a file's largest function is the one that
sets the decomposition problem.

| File | Functions over ceiling | Longest |
|---|---|---|
| `lib/core/src/nucleus/resolve/resolution_context.h` | 10 | 354 |
| `lib/xml/src/nucleus/xml/xml_source.cpp` | 3 | 203 |
| `lib/core/src/nucleus/schema/schema_registry.h` | 5 | 118 |
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

## Carried — test files over the 200-line ceiling

29 of 137 units. Test units are held to the same ceiling: the two most recent decomposition
commits on this branch split test files, not library files.

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
| `tests/token_cycle_test.cpp` | 202 |
| `tests/instance_addressing_test.cpp` | 202 |
