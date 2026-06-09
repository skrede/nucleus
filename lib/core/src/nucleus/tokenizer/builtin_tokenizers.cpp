#include "nucleus/format.h"

#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"

#include <span>
#include <cctype>
#include <string>
#include <cstdlib>
#include <utility>
#include <algorithm>

namespace nucleus {

namespace {

unexpected<resolve_error> arity_error(std::string_view fn, std::string_view want)
{
    return unexpected(resolve_error(resolve_errc::arg_count_mismatch,
                              nucleus::format("string.{} expects {}", fn, want)));
}

std::string ascii_map(std::string_view in, int (*op)(int))
{
    std::string out(in);
    for(char &c : out)
        c = static_cast<char>(op(static_cast<unsigned char>(c)));
    return out;
}

std::string ascii_trim(std::string_view in)
{
    std::size_t b = 0;
    std::size_t e = in.size();
    while(b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while(e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    return std::string(in.substr(b, e - b));
}

std::string replace_all(std::string_view in, std::string_view from, std::string_view to)
{
    if(from.empty())
        return std::string(in);
    std::string out;
    std::size_t pos = 0;
    for(std::size_t found; (found = in.find(from, pos)) != std::string_view::npos;)
    {
        out.append(in.substr(pos, found - pos));
        out.append(to);
        pos = found + from.size();
    }
    out.append(in.substr(pos));
    return out;
}

}

tokenizer make_env_tokenizer()
{
    tokenizer_builder builder("env");
    builder.set_wildcard([](std::string_view name) -> token_result {
        if(const char *value = std::getenv(std::string(name).c_str()))
            return std::string(value);
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("environment variable '{}' is not set", name)));
    });
    return std::move(builder).build();
}

tokenizer make_string_tokenizer()
{
    tokenizer_builder builder("string");

    builder.add_function("upper", [](std::span<const std::string> a) -> token_result {
        if(a.size() != 1) return arity_error("upper", "1 argument");
        return ascii_map(a[0], std::toupper);
    });
    builder.add_function("lower", [](std::span<const std::string> a) -> token_result {
        if(a.size() != 1) return arity_error("lower", "1 argument");
        return ascii_map(a[0], std::tolower);
    });
    builder.add_function("trim", [](std::span<const std::string> a) -> token_result {
        if(a.size() != 1) return arity_error("trim", "1 argument");
        return ascii_trim(a[0]);
    });
    builder.add_function("length", [](std::span<const std::string> a) -> token_result {
        if(a.size() != 1) return arity_error("length", "1 argument");
        return std::to_string(a[0].size());
    });
    builder.add_function("replace", [](std::span<const std::string> a) -> token_result {
        if(a.size() != 3) return arity_error("replace", "3 arguments (string, from, to)");
        return replace_all(a[0], a[1], a[2]);
    });
    builder.add_function("concat", [](std::span<const std::string> a) -> token_result {
        std::string out;
        for(const auto &part : a) out += part;
        return out;
    });
    builder.add_function("substr", [](std::span<const std::string> a) -> token_result {
        if(a.size() != 2 && a.size() != 3)
            return arity_error("substr", "2 or 3 arguments (string, pos[, count])");
        std::size_t pos = 0;
        try { pos = static_cast<std::size_t>(std::stoull(a[1])); }
        catch(...) { return unexpected(resolve_error(resolve_errc::arg_count_mismatch,
                                               "string.substr pos is not a number")); }
        if(pos > a[0].size())
            return unexpected(resolve_error(resolve_errc::arg_count_mismatch,
                                      "string.substr pos is past the end of the string"));
        if(a.size() == 2)
            return a[0].substr(pos);
        std::size_t count = 0;
        try { count = static_cast<std::size_t>(std::stoull(a[2])); }
        catch(...) { return unexpected(resolve_error(resolve_errc::arg_count_mismatch,
                                               "string.substr count is not a number")); }
        return a[0].substr(pos, count);
    });

    return std::move(builder).build();
}

}
