# Named tokenizer arguments

Tokenizer-function tokens — `${category.function(...)}` — take **named, typed**
arguments. The author of a tokenizer declares each argument's name and
fundamental type once, at `add_function`; a token writes its arguments as
`name=value` pairs in any order; and the framework binds the call against the
declaration — matching names, filling defaults, and coercing each resolved value
to its declared type — before the resolver closure runs. There is no positional
call form.

This is a host-UX feature: a misspelled argument name or an un-coercible value
becomes a precise, self-documenting diagnostic instead of an opaque failure.

## The call grammar

```
${string.replace(value='a.b.c', from='.', to='/')}      -> a/b/c
${string.substr(value=hello, pos=1, count=3)}           -> ell
${string.concat(values=['foo', 'bar'], separator='-')}  -> foo-bar
${time.utc(format='%Y-%m-%d')}                           -> 2023-11-14
${time.utc()}                                            -> (the declared default)
```

- **Named, order-independent.** Every argument is `name=value`; the order is
  irrelevant. `replace(from=a, value=aXa, to=b)` and
  `replace(value=aXa, to=b, from=a)` are the same call.
- **Optional with defaults.** An argument the author declares optional may be
  omitted — it then either takes its declared default (e.g. `time.utc`'s
  `format`) or is simply absent (e.g. `substr`'s `count`, which the closure
  queries with `has`).
- **List values** use square brackets: `values=['a', 'b', 'c']`. A list surfaces
  to the resolver as a `std::vector<std::string>`. Empty (`values=[]`) and
  single-element (`values=['x']`) lists are both legal.

### Quoting

Single (or double) quotes are **optional and lexical**: they group and protect a
literal, and are stripped. They never suppress resolution and never determine an
argument's type.

- Quote a literal to carry a top-level `,` or `]`, to preserve leading/trailing
  whitespace, or to write the empty string `''`.
- Bare values are trimmed; interior whitespace survives; a nested `${...}` is
  resolved per value (and per list element) before coercion.

```
${string.concat(values=['a,b', 'c]d'], separator='|')}  -> a,b|c]d
${string.replace(value=abc, from=b, to='')}             -> ac     ('' is the empty string)
${string.concat(values=[${env.REGION}, prod])}          -> <REGION>prod
```

> Escaping a literal `'` inside a quoted value is not supported in this release
> (the same posture as the deferred `$${` literal-brace escape). `''` therefore
> unambiguously means the empty string.

## Declaring arguments — `arg_spec`

`#include "nucleus/tokenizer/named_args.h"`

An argument is declared with an `arg_spec`. The fundamental types are
`arg_type::{string, integer, real, boolean}`; any of them may be a list.

```cpp
arg_spec::scalar("value", arg_type::string)                 // required string
arg_spec::scalar("pos",   arg_type::integer)                // required int
arg_spec::scalar("count", arg_type::integer).optional()     // optional, absent if omitted
arg_spec::scalar("separator", arg_type::string).with_default("")  // optional, defaulted
arg_spec::list("values", arg_type::string)                  // required list-of-string
```

- `scalar(name, type)` / `list(name, type)` build a **required** argument.
- `.optional()` makes it optional with no default — absent unless supplied.
- `.with_default(text)` makes it optional with a default that is coerced exactly
  like a supplied value.

A function declares its arguments as the `std::vector<arg_spec>` second parameter
of `add_function`:

```cpp
builder.add_function("substr",
    {arg_spec::scalar("value", arg_type::string),
     arg_spec::scalar("pos",   arg_type::integer),
     arg_spec::scalar("count", arg_type::integer).optional()},
    [](const nucleus::named_args &a) -> nucleus::token_result {
        const std::string &value = a.string("value");
        const long long pos = a.integer("pos");
        if(!a.has("count"))
            return value.substr(static_cast<std::size_t>(pos));
        return value.substr(static_cast<std::size_t>(pos),
                            static_cast<std::size_t>(a.integer("count")));
    });
```

## Reading arguments — `named_args`

The closure receives a `named_args`. The framework has already validated names,
filled defaults, enforced the scalar-vs-list shape, and coerced each value, so
the accessors return typed values directly:

| Accessor | Returns | For an argument declared |
| --- | --- | --- |
| `has(name)` | `bool` | any (false if optional-and-absent) |
| `string(name)` | `const std::string &` | scalar `string` |
| `integer(name)` | `long long` | scalar `integer` |
| `real(name)` | `double` | scalar `real` |
| `boolean(name)` | `bool` | scalar `boolean` |
| `strings(name)` | `std::vector<std::string>` | list-of-`string` |
| `as_list(name)` | `const std::vector<arg_scalar> &` | any list (typed elements) |

Token *results* are always strings — a token expands to text spliced back into
the surrounding value. Typing applies to **arguments**, not to the result.

## Diagnostics

Argument errors are loud and named, in the same family as the unknown-key
"did you mean?" diagnostics:

- **Unknown argument name** → `resolve_errc::unknown_argument`, suggesting the
  nearest declared name and naming the function:
  `unknown argument 'form' for 'string.replace' -- did you mean 'from'?`
- **Un-coercible value** → `resolve_errc::type_mismatch`:
  `argument 'pos' expects an integer, got 'abc'`.
- **Missing required argument** → `resolve_errc::missing_argument`:
  `missing required argument 'to' for 'string.replace'`.
- **Wrong shape** (a scalar argument given a `[ ]` list, or vice versa) →
  `resolve_errc::type_mismatch`.

## See also

- [`examples/time_tokenizer.cpp`](../examples/time_tokenizer.cpp) — a host
  authoring a `${time.*}` tokenizer with an optional `format=` argument and a
  list-valued `concat` showcase.
- [Seams you extend — Custom tokenizers](api-extending.md#tokenizers) — the
  `tokenizer_builder` surface this argument model plugs into.
- The built-in `string.*` functions are summarized in
  [`builtin_tokenizers.h`](../lib/core/src/nucleus/tokenizer/builtin_tokenizers.h).
