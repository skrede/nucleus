#include "nucleus/tokenizer/token_lexer.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using nucleus::lex_token;
using nucleus::resolve_errc;

TEST_CASE("field form lexes category and name", "[lexer]")
{
    auto r = lex_token("${env.HOME}");
    REQUIRE(r.has_value());
    CHECK(r.value().category == "env");
    CHECK(r.value().name == "HOME");
    CHECK_FALSE(r.value().is_function);
    CHECK(r.value().args.empty());
}

TEST_CASE("function form lexes args on top-level commas", "[lexer]")
{
    auto r = lex_token("${string.replace(a, b, c)}");
    REQUIRE(r.has_value());
    CHECK(r.value().category == "string");
    CHECK(r.value().name == "replace");
    CHECK(r.value().is_function);
    REQUIRE(r.value().args.size() == 3);
    CHECK(r.value().args[0] == "a");
    CHECK(r.value().args[1] == "b");
    CHECK(r.value().args[2] == "c");
}

TEST_CASE("zero-arg function lexes as a function with no args", "[lexer]")
{
    auto r = lex_token("${uuid.v4()}");
    REQUIRE(r.has_value());
    CHECK(r.value().is_function);
    CHECK(r.value().args.empty());
}

TEST_CASE("quoted arg strips quotes and protects commas", "[lexer]")
{
    auto r = lex_token("${string.concat('a,b', c)}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 2);
    CHECK(r.value().args[0] == "a,b");
    CHECK(r.value().args[1] == "c");
}

TEST_CASE("nested token inside an arg is preserved verbatim", "[lexer]")
{
    auto r = lex_token("${string.upper(${env.HOME})}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 1);
    CHECK(r.value().args[0] == "${env.HOME}");
}

TEST_CASE("interior whitespace survives, boundary whitespace trimmed", "[lexer]")
{
    auto r = lex_token("${string.concat( a b ,  c )}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 2);
    CHECK(r.value().args[0] == "a b");
    CHECK(r.value().args[1] == "c");
}

TEST_CASE("malformed tokens report parse_error", "[lexer]")
{
    auto missing_dot = lex_token("${envHOME}");
    REQUIRE_FALSE(missing_dot.has_value());
    CHECK(missing_dot.error().code == resolve_errc::parse_error);

    auto empty_name = lex_token("${env.}");
    REQUIRE_FALSE(empty_name.has_value());

    auto unbalanced = lex_token("${string.upper(a}");
    REQUIRE_FALSE(unbalanced.has_value());

    auto not_a_token = lex_token("plain");
    REQUIRE_FALSE(not_a_token.has_value());

    auto stray = lex_token("${string.upper(a)x}");
    REQUIRE_FALSE(stray.has_value());
}
