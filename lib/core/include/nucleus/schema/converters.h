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
// Numeric converters use std::from_chars for locale-independent parsing. The
// integral overloads are universally available; the floating-point ones are
// not -- Apple's libc++ (through at least the Xcode 15/16 toolchains) ships the
// integral from_chars while leaving float/double =deleted. The float and double
// converters therefore route through detail::fp_from_chars, which uses
// std::from_chars where <charconv> is complete and falls back to strtof/strtod
// otherwise (see the shim below for the exact capability probe and the locale
// caveat on the fallback path).
//
// Converters must not throw; return unexpected() for any conversion error.

#include "nucleus/expected.h"

#include "nucleus/schema/schema.h"

#include <any>
#include <cctype>
#include <cerrno>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <charconv>
#include <concepts>
#include <optional>
#include <algorithm>
#include <typeindex>
#include <functional>
#include <string_view>
#include <type_traits>

namespace nucleus {

// ---------------------------------------------------------------------------
// Floating-point from_chars shim
//
// std::from_chars for float/double belongs to <charconv>, but several shipping
// standard libraries provide the integral overloads while still *lacking* the
// floating-point ones -- notably Apple's libc++ through (at least) the Xcode 15
// and 16 toolchains, where the float/double overloads are explicitly =deleted.
// A direct std::from_chars call on a float there is a hard compile error.
//
// __cpp_lib_to_chars is defined by the standard library only once <charconv> is
// complete *including* the floating-point overloads, so it cleanly separates the
// full implementations (libstdc++ 11+, MSVC STL, non-Apple libc++ 14+) from the
// integral-only ones. Where it is absent we fall back to strtof/strtod over a
// NUL-terminated copy and translate the outcome back into a from_chars-shaped
// {ptr, ec} expressed in the original view's coordinates, so the dispatch in the
// converters below is byte-for-byte identical on every toolchain.
//
// NOTE: on the fallback path the parse follows the active C locale's numeric
// conventions; the std::from_chars path (every modern toolchain) is
// unconditionally locale-independent.
//
// Define NUCLEUS_FORCE_FP_FROM_CHARS_FALLBACK to exercise the strtof/strtod path
// on a toolchain that does have floating-point from_chars (used by the tests to
// cover the fallback everywhere).
// ---------------------------------------------------------------------------
namespace detail {

struct fp_parse_result
{
    const char *ptr;
    std::errc   ec;
};

#if defined(__cpp_lib_to_chars) && !defined(NUCLEUS_FORCE_FP_FROM_CHARS_FALLBACK)

template<typename Float>
[[nodiscard]] inline fp_parse_result fp_from_chars(std::string_view sv, Float &out)
{
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return {ptr, ec};
}

#else

template<typename Float>
[[nodiscard]] inline fp_parse_result fp_from_chars(std::string_view sv, Float &out)
{
    // from_chars rejects leading whitespace and a leading '+'; strtof/strtod
    // accept both. Reject them up front -- reported as zero characters consumed
    // (invalid_argument at the start), exactly as from_chars would, so the two
    // paths agree on these inputs.
    const char lead = sv.front();
    if(lead == '+' || lead == ' ' || lead == '\t' || lead == '\n'
       || lead == '\v' || lead == '\f' || lead == '\r')
        return {sv.data(), std::errc::invalid_argument};

    // strtof/strtod scan to a NUL terminator and would read past a view that is
    // a window into a larger buffer, so parse a terminated copy and map the
    // consumed length back onto the original view's coordinates.
    const std::string buf(sv);
    const char *const begin = buf.c_str();
    char *end = nullptr;
    errno = 0;
    Float value{};
    if constexpr(std::is_same_v<Float, float>)
        value = std::strtof(begin, &end);
    else
        value = std::strtod(begin, &end);

    if(end == begin)
        return {sv.data(), std::errc::invalid_argument};
    const auto consumed = static_cast<std::size_t>(end - begin);
    if(errno == ERANGE)
        return {sv.data() + consumed, std::errc::result_out_of_range};
    out = value;
    return {sv.data() + consumed, std::errc{}};
}

#endif

}

// Maps a from_chars-shaped outcome onto the converter error vocabulary shared
// by every numeric width:
//   success but ptr short of the end  -> "trailing characters after value"
//   errc::result_out_of_range         -> "value out of range for type"
//   errc::invalid_argument, zero characters consumed:
//        leading '-'                  -> "value out of range for type"
//                                        (only reachable for unsigned targets: a
//                                        signed out-of-range negative is already
//                                        result_out_of_range)
//        anything else (incl. '+')    -> "invalid characters in value"
//                                        (from_chars never accepts a leading '+';
//                                        that is syntax, not range)
//   errc::invalid_argument, partial consume -> "trailing characters after value"
namespace detail {

template<typename T>
[[nodiscard]] inline expected<std::any, std::string>
classify_numeric_parse(std::string_view sv, const char *ptr, std::errc ec, T out)
{
    if(ec == std::errc{})
    {
        if(ptr != sv.data() + sv.size())
            return unexpected(std::string("trailing characters after value"));
        return std::any(out);
    }
    if(ec == std::errc::result_out_of_range)
        return unexpected(std::string("value out of range for type"));
    // errc::invalid_argument
    if(ptr == sv.data())
    {
        if(sv.front() == '-')
            return unexpected(std::string("value out of range for type"));
        return unexpected(std::string("invalid characters in value"));
    }
    return unexpected(std::string("trailing characters after value"));
}

}

// Returns a converter lambda for the built-in scalar type T (the set in the
// header comment above; anything else is a loud compile error). Exposed so a
// host can compose it (e.g. wrap it with extra validation) without
// re-implementing the low-level parsing. bool and char are carved out before
// the std::integral branch because both satisfy the concept yet have their own
// vocabularies (true/false/1/0; exactly one byte).
template<typename T>
[[nodiscard]] std::function<expected<std::any, std::string>(std::string_view)>
make_scalar_converter()
{
    if constexpr(std::is_same_v<T, bool>)
    {
        // Accepts (case-insensitive) "true", "false", "1", "0"; everything else errors.
        return [](std::string_view sv) -> expected<std::any, std::string> {
            if(sv.empty())
                return unexpected(std::string("empty input"));
            std::string lower(sv);
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if(lower == "true" || lower == "1")
                return std::any(true);
            if(lower == "false" || lower == "0")
                return std::any(false);
            return unexpected(std::string("expected true/false/1/0"));
        };
    }
    else if constexpr(std::is_same_v<T, char>)
    {
        return [](std::string_view sv) -> expected<std::any, std::string> {
            if(sv.size() != 1)
                return unexpected(std::string("expected exactly one character"));
            return std::any(static_cast<char>(sv[0]));
        };
    }
    else if constexpr(std::is_same_v<T, std::string>)
    {
        return [](std::string_view sv) -> expected<std::any, std::string> {
            return std::any(std::string(sv));
        };
    }
    else if constexpr(std::integral<T>)
    {
        return [](std::string_view sv) -> expected<std::any, std::string> {
            if(sv.empty())
                return unexpected(std::string("empty input"));
            T out{};
            auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
            return detail::classify_numeric_parse(sv, ptr, ec, out);
        };
    }
    else if constexpr(std::floating_point<T>)
    {
        return [](std::string_view sv) -> expected<std::any, std::string> {
            if(sv.empty())
                return unexpected(std::string("empty input"));
            T out{};
            auto [ptr, ec] = detail::fp_from_chars(sv, out);
            return detail::classify_numeric_parse(sv, ptr, ec, out);
        };
    }
    else
    {
        static_assert(std::is_same_v<T, void>,
                      "make_scalar_converter<T>: T is not a built-in scalar "
                      "(see the supported-type list in this header)");
    }
}

// typed_element<T>(name, at, conv) -- attaches a converter and type identity to
// a schema_element. The returned element is otherwise identical to element(name, at).
template<typename T>
[[nodiscard]] schema_element typed_element(std::string name, anchor at,
    std::function<expected<std::any, std::string>(std::string_view)> conv)
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

// repeated_typed_element<T>(name, at, conv) -- a single element that is BOTH a
// repeated collection AND typed: every occurrence is kept in document/fold order
// and each is converted to T. Exactly typed_element<T> plus el.repeated = true.
template<typename T>
[[nodiscard]] schema_element repeated_typed_element(std::string name, anchor at,
    std::function<expected<std::any, std::string>(std::string_view)> conv)
{
    schema_element el = typed_element<T>(std::move(name), std::move(at), std::move(conv));
    el.repeated = true;
    return el;
}

// repeated_typed_element<T>(name, at) -- built-in-scalar overload: a typed
// repeated collection converted by the built-in scalar converter for T. Only
// instantiable for types with a make_scalar_converter specialization.
template<typename T>
[[nodiscard]] schema_element repeated_typed_element(std::string name, anchor at)
{
    return repeated_typed_element<T>(std::move(name), std::move(at), make_scalar_converter<T>());
}

// registered_element<T>(name, at) -- declares a typed element carrying ONLY a
// type identity and NO inline converter. The converter is supplied at resolve by
// the configuration_space's converter registry (keyed by this type_identity); a
// per-element converter is left empty so the registry's converter applies.
template<typename T>
[[nodiscard]] schema_element registered_element(std::string name, anchor at)
{
    schema_element el;
    el.name = std::move(name);
    el.at = std::move(at);
    el.type_identity = std::type_index(typeid(T));
    return el;
}

}

#endif
