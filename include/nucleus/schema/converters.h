#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CONVERTERS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CONVERTERS_H

// typed_element<T> factory and the built-in scalar converter set.
//
// make_scalar_converter<T> is defined for the following types only:
//   int8_t, int16_t, int32_t, int64_t
//   uint8_t, uint16_t, uint32_t, uint64_t
//   float, double
//   bool
//   char
//   std::string
//
// Numeric converters use std::from_chars for locale-independent parsing.
// Toolchain floor required for floating-point from_chars:
//   GCC 11+, LLVM Clang 14+, MSVC 19.29+, Apple Clang 15+
// (Apple's libc++ gained FP from_chars only in Xcode 15; plain "Clang 14+"
// is NOT sufficient on macOS.)
//
// Converters must not throw; return fail() for any conversion error.

#include "nucleus/schema/schema.h"
#include "nucleus/result.h"

#include <any>
#include <string>
#include <vector>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <functional>
#include <string_view>
#include <typeindex>

namespace nucleus {

// Returns a converter lambda for the built-in scalar type T. Only instantiable
// for the types listed in the toolchain floor comment above. Exposed so a host
// can compose it (e.g. wrap it with extra validation) without re-implementing
// the low-level parsing.
template<typename T>
[[nodiscard]] std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter()
{
    // Intentionally not defined for arbitrary T -- only explicit specializations
    // below are valid instantiations.
    static_assert(sizeof(T) == 0, "make_scalar_converter<T> has no specialization for this type");
    return {};
}

// Integer and float helpers -- dispatch follows the plan exactly:
//   1. sv.empty()           -> "empty input"
//   2. from_chars succeeds but ptr != sv.data()+sv.size()
//                           -> "trailing characters after value"
//   3. errc::result_out_of_range -> "value out of range for type"
//   4. errc::invalid_argument on non-empty input:
//        ptr == sv.data()   -> zero characters consumed (bad leading char,
//                              including '-' into unsigned)
//                           -> "value out of range for type"
//        ptr  > sv.data()   -> valid prefix with trailing garbage, but since
//                              from_chars stops on the first bad char and we
//                              already checked ptr==end above, this branch is
//                              "trailing characters after value"

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<int8_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        int8_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        // errc::invalid_argument
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<int16_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        int16_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<int32_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        int32_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<int64_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        int64_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<uint8_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        uint8_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<uint16_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        uint16_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<uint32_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        uint32_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<uint64_t>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        uint64_t out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<float>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        float out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<double>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        double out{};
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        if(ec == std::errc{})
        {
            if(ptr != sv.data() + sv.size())
                return fail(std::string("trailing characters after value"));
            return std::any(out);
        }
        if(ec == std::errc::result_out_of_range)
            return fail(std::string("value out of range for type"));
        if(ptr == sv.data())
            return fail(std::string("value out of range for type"));
        return fail(std::string("trailing characters after value"));
    };
}

// bool: accepts (case-insensitive) "true", "false", "1", "0".
// All other input is an error: "expected true/false/1/0".
template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<bool>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.empty())
            return fail(std::string("empty input"));
        std::string lower(sv);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if(lower == "true" || lower == "1")
            return std::any(true);
        if(lower == "false" || lower == "0")
            return std::any(false);
        return fail(std::string("expected true/false/1/0"));
    };
}

// char: exactly one byte -- longer or empty input is an error.
template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<char>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        if(sv.size() != 1)
            return fail(std::string("expected exactly one character"));
        return std::any(static_cast<char>(sv[0]));
    };
}

// std::string: passthrough -- always succeeds.
template<>
[[nodiscard]] inline std::function<result<std::any, std::string>(std::string_view)>
make_scalar_converter<std::string>()
{
    return [](std::string_view sv) -> result<std::any, std::string> {
        return std::any(std::string(sv));
    };
}

// typed_element<T>(name, at, conv) -- attaches a converter and type identity to
// a schema_element. The returned element is otherwise identical to element(name, at).
template<typename T>
[[nodiscard]] schema_element typed_element(std::string name, anchor at,
    std::function<result<std::any, std::string>(std::string_view)> conv)
{
    schema_element el;
    el.name = std::move(name);
    el.at = std::move(at);
    el.converter = std::move(conv);
    el.type_identity = std::type_index(typeid(T));
    return el;
}

// typed_element<T>(name, at) -- no-lambda overload; uses the built-in scalar
// converter for T. Only instantiable for types that have a make_scalar_converter
// specialization (the set listed in the header comment).
template<typename T>
[[nodiscard]] schema_element typed_element(std::string name, anchor at)
{
    return typed_element<T>(std::move(name), std::move(at), make_scalar_converter<T>());
}

}

#endif
