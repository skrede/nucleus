#ifndef HPP_GUARD_NUCLEUS_TESTS_SUPPORT_ESCAPED_ECHO_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_SUPPORT_ESCAPED_ECHO_TEST_SUPPORT_H

#include "nucleus/error.h"
#include "nucleus/expected.h"

#include "nucleus/schema/anchor.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/utility/escaped_text.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace nucleus::escaped_echo_test {

// An OSC title-set sequence and U+202E RIGHT-TO-LEFT OVERRIDE: one rewrites the terminal, one reorders the line.
inline const std::vector<std::string> terminal_controls{"\x1b]0;pwn\x07", "\xe2\x80\xae"};

inline std::uint8_t byte_of(char c)
{
    return static_cast<std::uint8_t>(c);
}

inline bool printable_ascii(const std::string &text)
{
    return std::ranges::all_of(text, [](char c)
                               { return byte_of(c) >= 0x20 && byte_of(c) < 0x7f; });
}

inline std::string framed(const std::string &control)
{
    return "x" + control + "y";
}

inline anchor under(const char *text)
{
    auto parsed = key_path::parse(text);
    REQUIRE(parsed);
    return anchor::keyspace(parsed.value());
}

inline std::string registration_refusal(const expected<void, error> &result, errc code = errc::rejected_registration)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == code);
    return result.error().message;
}

inline void require_escaped_echo(const std::string &message, const char *rule, const std::string &echoed)
{
    REQUIRE(message.find(rule) != std::string::npos);
    REQUIRE(message.find(escaped_text(echoed)) != std::string::npos);
    REQUIRE(printable_ascii(message));
}

}

#endif
