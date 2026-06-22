#include "nucleus/format.h"

#include "nucleus/tokenizer/named_args.h"
#include "nucleus/schema/converters.h"

#include <any>
#include <string>

namespace nucleus {

const named_args::bound_arg *named_args::find(std::string_view name) const noexcept
{
    for(const auto &arg : m_args)
        if(arg.name == name)
            return &arg;
    return nullptr;
}

const std::string &named_args::string(std::string_view name) const
{
    static const std::string empty;
    if(const bound_arg *a = find(name); a && !a->values.empty())
        if(const auto *s = std::get_if<std::string>(&a->values.front()))
            return *s;
    return empty;
}

long long named_args::integer(std::string_view name) const noexcept
{
    if(const bound_arg *a = find(name); a && !a->values.empty())
        if(const auto *v = std::get_if<long long>(&a->values.front()))
            return *v;
    return 0;
}

double named_args::real(std::string_view name) const noexcept
{
    if(const bound_arg *a = find(name); a && !a->values.empty())
        if(const auto *v = std::get_if<double>(&a->values.front()))
            return *v;
    return 0.0;
}

bool named_args::boolean(std::string_view name) const noexcept
{
    if(const bound_arg *a = find(name); a && !a->values.empty())
        if(const auto *v = std::get_if<bool>(&a->values.front()))
            return *v;
    return false;
}

std::vector<std::string> named_args::strings(std::string_view name) const
{
    std::vector<std::string> out;
    if(const bound_arg *a = find(name))
        for(const auto &v : a->values)
            if(const auto *s = std::get_if<std::string>(&v))
                out.push_back(*s);
    return out;
}

const std::vector<arg_scalar> &named_args::as_list(std::string_view name) const
{
    static const std::vector<arg_scalar> empty;
    const bound_arg *a = find(name);
    return a ? a->values : empty;
}

// Coercion reuses the locale-independent, non-throwing scalar converters the
// schema layer already ships (from_chars with full-consumption checks and the
// Apple-libc++ floating-point shim) -- the same engine that validates typed
// configuration fields, so an argument coerces exactly as a typed leaf would.
expected<arg_scalar, resolve_error> coerce_scalar(arg_type type,
                                                  std::string_view name,
                                                  std::string_view text)
{
    auto type_error = [&]() {
        return unexpected(resolve_error(resolve_errc::type_mismatch,
            nucleus::format("argument '{}' expects {}, got '{}'", name, to_string(type), text)));
    };

    switch(type)
    {
    case arg_type::string:
        return arg_scalar(std::string(text));
    case arg_type::integer:
        if(auto r = make_scalar_converter<long long>()(text); r)
            return arg_scalar(std::any_cast<long long>(r.value()));
        return type_error();
    case arg_type::real:
        if(auto r = make_scalar_converter<double>()(text); r)
            return arg_scalar(std::any_cast<double>(r.value()));
        return type_error();
    case arg_type::boolean:
        if(auto r = make_scalar_converter<bool>()(text); r)
            return arg_scalar(std::any_cast<bool>(r.value()));
        return type_error();
    }
    return type_error();
}

}
