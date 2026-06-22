#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_NAMED_ARGS_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_NAMED_ARGS_H

#include "nucleus/expected.h"

#include "nucleus/tokenizer/resolve_error.h"

#include <string>
#include <vector>
#include <variant>
#include <utility>
#include <string_view>

namespace nucleus {

// The fundamental type a tokenizer-function argument is declared as. Coercion of
// the resolved argument text targets one of these; list arguments coerce each
// element to the same element type. Token results stay strings -- typing applies
// to arguments only.
enum class arg_type { string, integer, real, boolean };

// A human-readable article+noun for a type, for the typed diagnostic
// ("argument 'pos' expects an integer, got 'abc'").
inline const char *to_string(arg_type type) noexcept
{
    switch(type)
    {
    case arg_type::string: return "a string";
    case arg_type::integer: return "an integer";
    case arg_type::real: return "a number";
    case arg_type::boolean: return "a boolean";
    }
    return "a value";
}

// One coerced argument value. Lists hold one of these per element.
using arg_scalar = std::variant<std::string, long long, double, bool>;

// A single parsed argument in its pre-coercion string form: the name, whether it
// was written as a list, and the (possibly token-bearing) value text. The lexer
// produces these with raw values; the resolver re-emits the same shape with each
// value resolved, then dispatch coerces them per the declared arg_spec. A scalar
// argument holds exactly one value.
struct token_argument
{
    std::string name;
    bool is_list = false;
    std::vector<std::string> values;
};

// The author's declaration of one named argument: its name, fundamental type,
// whether it is a list, and its optionality. A required argument must be supplied;
// an optional one is either absent (queried via named_args::has) or carries a
// default that is coerced and always present. Built via the scalar()/list()
// factories and the fluent optional()/with_default() modifiers.
struct arg_spec
{
    std::string name;
    arg_type type = arg_type::string;
    bool is_list = false;
    bool required = true;
    bool has_default = false;
    std::string default_text;

    static arg_spec scalar(std::string name, arg_type type)
    {
        return arg_spec{std::move(name), type, false, true, false, {}};
    }

    static arg_spec list(std::string name, arg_type type)
    {
        return arg_spec{std::move(name), type, true, true, false, {}};
    }

    arg_spec &optional()
    {
        required = false;
        has_default = false;
        return *this;
    }

    arg_spec &with_default(std::string text)
    {
        required = false;
        has_default = true;
        default_text = std::move(text);
        return *this;
    }
};

// The coerced argument values handed to a function resolver. The framework binds
// and coerces every declared argument once at dispatch, so a resolver reads typed
// values without re-parsing. Accessors are total: an absent name or a type the
// argument was not declared as yields the type's default rather than throwing --
// a resolver reads exactly what it declared.
class named_args
{
public:
    struct bound_arg
    {
        std::string name;
        bool is_list = false;
        std::vector<arg_scalar> values;
    };

    void add(bound_arg arg) { m_args.push_back(std::move(arg)); }

    bool has(std::string_view name) const noexcept { return find(name) != nullptr; }

    const std::string &string(std::string_view name) const;
    long long integer(std::string_view name) const noexcept;
    double real(std::string_view name) const noexcept;
    bool boolean(std::string_view name) const noexcept;

    // List-of-string: the ARG-02 vector<string> contract. as_list() exposes the
    // raw coerced elements for a typed (non-string) list.
    std::vector<std::string> strings(std::string_view name) const;
    const std::vector<arg_scalar> &as_list(std::string_view name) const;

private:
    const bound_arg *find(std::string_view name) const noexcept;

    std::vector<bound_arg> m_args;
};

// Coerces one resolved argument text to the declared fundamental type. On failure
// returns a type_mismatch error naming the argument and the expected type.
expected<arg_scalar, resolve_error> coerce_scalar(arg_type type,
                                                  std::string_view name,
                                                  std::string_view text);

}

#endif
