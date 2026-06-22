#include "nucleus/format.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/diagnostics/key_suggester.h"

#include <span>
#include <string>
#include <vector>
#include <utility>

namespace nucleus {

namespace {

// Binds a call's named arguments against a function's declared arg_specs:
// rejects an unknown argument name (suggesting the nearest declared name), fills
// required/defaulted arguments, enforces the scalar-vs-list shape, and coerces
// every value to its declared type. The resulting named_args is what the resolver
// closure reads -- all per-argument validation happens here, once, at the boundary.
expected<named_args, resolve_error> bind_named_args(std::string_view category,
                                                    const token_function &fn,
                                                    std::span<const token_argument> provided)
{
    std::vector<std::string> declared;
    declared.reserve(fn.params.size());
    for(const auto &spec : fn.params)
        declared.push_back(spec.name);

    auto declares = [&](std::string_view n) {
        for(const auto &spec : fn.params)
            if(spec.name == n) return true;
        return false;
    };

    for(const auto &arg : provided)
        if(!declares(arg.name))
        {
            auto near = suggest_keys(arg.name, declared, 1);
            std::string hint = near.empty() ? std::string()
                                            : nucleus::format(" -- did you mean '{}'?", near.front());
            return unexpected(resolve_error(resolve_errc::unknown_argument,
                nucleus::format("unknown argument '{}' for '{}.{}'{}", arg.name, category, fn.name, hint)));
        }

    named_args out;
    for(const auto &spec : fn.params)
    {
        const token_argument *match = nullptr;
        for(const auto &arg : provided)
            if(arg.name == spec.name) { match = &arg; break; }

        if(match == nullptr)
        {
            if(spec.required)
                return unexpected(resolve_error(resolve_errc::missing_argument,
                    nucleus::format("missing required argument '{}' for '{}.{}'",
                                    spec.name, category, fn.name)));
            if(!spec.has_default)
                continue;  // optional and absent: named_args::has() reports false
            auto coerced = coerce_scalar(spec.type, spec.name, spec.default_text);
            if(!coerced) return unexpected(std::move(coerced).error());
            out.add({spec.name, spec.is_list, {std::move(coerced).value()}});
            continue;
        }

        if(spec.is_list != match->is_list)
            return unexpected(resolve_error(resolve_errc::type_mismatch,
                nucleus::format("argument '{}' for '{}.{}' expects {}", spec.name, category, fn.name,
                                spec.is_list ? "a list value [ ... ]" : "a single value")));

        named_args::bound_arg bound{spec.name, spec.is_list, {}};
        bound.values.reserve(match->values.size());
        for(const auto &text : match->values)
        {
            auto coerced = coerce_scalar(spec.type, spec.name, text);
            if(!coerced) return unexpected(std::move(coerced).error());
            bound.values.push_back(std::move(coerced).value());
        }
        out.add(std::move(bound));
    }
    return out;
}

}

token_result tokenizer::resolve_field(std::string_view name) const
{
    for(const auto &field : m_fields)
        if(field.name == name)
            return field.resolve();
    if(m_wildcard)
        return m_wildcard(name);
    return unexpected(resolve_error(resolve_errc::missing_field,
                              nucleus::format("'{}' has no field '{}'", m_category, name)));
}

token_result tokenizer::resolve_function(std::string_view name,
                                         std::span<const token_argument> args) const
{
    for(const auto &fn : m_functions)
        if(fn.name == name)
        {
            auto bound = bind_named_args(m_category, fn, args);
            if(!bound) return unexpected(std::move(bound).error());
            return fn.resolve(bound.value());
        }
    return unexpected(resolve_error(resolve_errc::unknown_function,
                              nucleus::format("'{}' has no function '{}'", m_category, name)));
}

}
