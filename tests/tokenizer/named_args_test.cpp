// Named & typed tokenizer-function arguments: order independence,
// list values, lexical quoting / empty string, typed coercion, and the loud
// did-you-mean / typed diagnostics. Exercised through resolve_tokens against the
// core env + string tokenizers.

#include "nucleus/identity.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstdlib>

using nucleus::owner_token;
using nucleus::resolve_errc;
using nucleus::resolve_tokens;
using nucleus::tokenizer_registry;

namespace {

tokenizer_registry core_registry()
{
    tokenizer_registry r;
    r.add(nucleus::make_env_tokenizer(), owner_token{});
    r.add(nucleus::make_string_tokenizer(), owner_token{});
    return r;
}

}

TEST_CASE("named arguments are order-independent", "[named-args]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.replace(from=a, value=aXa, to=b)}", reg).value() == "bXb");
    CHECK(resolve_tokens("${string.replace(value=aXa, to=b, from=a)}", reg).value() == "bXb");
}

TEST_CASE("a list value joins with the separator", "[named-args][list]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.concat(values=[a, b, c], separator='-')}", reg).value() == "a-b-c");
    CHECK(resolve_tokens("${string.concat(values=[a, b, c])}", reg).value() == "abc");
}

TEST_CASE("an empty list yields the empty string", "[named-args][list]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.concat(values=[])}", reg).value().empty());
}

TEST_CASE("a quoted element protects a comma and a closing bracket", "[named-args][list]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.concat(values=['a,b', 'c]d'], separator='|')}", reg).value()
          == "a,b|c]d");
}

TEST_CASE("the empty quoted literal is the empty string", "[named-args]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.replace(value=abc, from=b, to='')}", reg).value() == "ac");
}

TEST_CASE("a token inside a list element resolves per element", "[named-args][list]")
{
#ifdef _WIN32
    _putenv_s("NUCLEUS_ARG_ELEM", "foo");
#else
    setenv("NUCLEUS_ARG_ELEM", "foo", 1);
#endif
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.concat(values=[${env.NUCLEUS_ARG_ELEM}, bar])}", reg).value()
          == "foobar");
}

TEST_CASE("an optional argument is absent unless supplied", "[named-args]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.substr(value=hello, pos=1)}", reg).value() == "ello");
    CHECK(resolve_tokens("${string.substr(value=hello, pos=1, count=3)}", reg).value() == "ell");
}

TEST_CASE("a value that fails to coerce is a typed diagnostic", "[named-args][diag]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("${string.substr(value=hi, pos=abc)}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::type_mismatch);
    CHECK(r.error().message.find("pos") != std::string::npos);
    CHECK(r.error().message.find("integer") != std::string::npos);
}

TEST_CASE("an unknown argument name suggests the nearest and names the function", "[named-args][diag]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("${string.replace(value=x, form=a, to=b)}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::unknown_argument);
    CHECK(r.error().message.find("string.replace") != std::string::npos);
    CHECK(r.error().message.find("did you mean 'from'") != std::string::npos);
}

TEST_CASE("a missing required argument is a named error", "[named-args][diag]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("${string.replace(value=x, from=a)}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::missing_argument);
    CHECK(r.error().message.find("to") != std::string::npos);
}

TEST_CASE("a scalar argument given a list value is a type error", "[named-args][diag]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("${string.upper(value=[a, b])}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::type_mismatch);
}

TEST_CASE("a closing brace inside a quoted function argument does not end the token",
          "[named-args][quoting]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.replace(value=abc, from=b, to='}')}", reg).value() == "a}c");
    CHECK(resolve_tokens("${string.concat(values=['}', 'x'], separator='-')}", reg).value() == "}-x");
}
