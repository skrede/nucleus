#include "nucleus/format.h"

#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"

#include <cctype>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

namespace {

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
    for(std::size_t found = 0; (found = in.find(from, pos)) != std::string_view::npos;)
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
        // std::getenv is the only portable lookup; MSVC's C4996 deprecation
        // pushes the non-standard _dupenv_s instead, so it is suppressed here.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *value = std::getenv(std::string(name).c_str());
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if(value != nullptr)
            return std::string(value);
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("environment variable '{}' is not set", name)));
    });
    return std::move(builder).build();
}

tokenizer make_string_tokenizer()
{
    using nucleus::arg_type;

    tokenizer_builder builder("string");

    auto value = [] { return arg_spec::scalar("value", arg_type::string); };

    builder.add_function("upper", {value()}, [](const named_args &a) -> token_result {
        return ascii_map(a.string("value"), std::toupper);
    });
    builder.add_function("lower", {value()}, [](const named_args &a) -> token_result {
        return ascii_map(a.string("value"), std::tolower);
    });
    builder.add_function("trim", {value()}, [](const named_args &a) -> token_result {
        return ascii_trim(a.string("value"));
    });
    builder.add_function("length", {value()}, [](const named_args &a) -> token_result {
        return std::to_string(a.string("value").size());
    });
    builder.add_function("replace",
        {value(), arg_spec::scalar("from", arg_type::string), arg_spec::scalar("to", arg_type::string)},
        [](const named_args &a) -> token_result {
            return replace_all(a.string("value"), a.string("from"), a.string("to"));
        });
    builder.add_function("concat",
        {arg_spec::list("values", arg_type::string),
         arg_spec::scalar("separator", arg_type::string).with_default("")},
        [](const named_args &a) -> token_result {
            std::string out;
            const std::string &separator = a.string("separator");
            const std::vector<std::string> parts = a.strings("values");
            for(std::size_t i = 0; i < parts.size(); ++i)
            {
                if(i != 0) out += separator;
                out += parts[i];
            }
            return out;
        });
    builder.add_function("substr",
        {value(), arg_spec::scalar("pos", arg_type::integer),
         arg_spec::scalar("count", arg_type::integer).optional()},
        [](const named_args &a) -> token_result {
            const std::string &text = a.string("value");
            const long long pos = a.integer("pos");
            if(pos < 0 || static_cast<std::size_t>(pos) > text.size())
                return unexpected(resolve_error(resolve_errc::type_mismatch,
                    nucleus::format("argument 'pos' ({}) is past the end of 'value' (length {})",
                                    pos, text.size())));
            const auto start = static_cast<std::size_t>(pos);
            if(!a.has("count"))
                return text.substr(start);
            const long long count = a.integer("count");
            if(count < 0)
                return unexpected(resolve_error(resolve_errc::type_mismatch,
                    nucleus::format("argument 'count' ({}) must not be negative", count)));
            return text.substr(start, static_cast<std::size_t>(count));
        });

    return std::move(builder).build();
}

}
