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

TEST_CASE("function form lexes named args on top-level commas", "[lexer]")
{
    auto r = lex_token("${string.replace(value=a, from=b, to=c)}");
    REQUIRE(r.has_value());
    CHECK(r.value().category == "string");
    CHECK(r.value().name == "replace");
    CHECK(r.value().is_function);
    REQUIRE(r.value().args.size() == 3);
    CHECK(r.value().args[0].name == "value");
    CHECK_FALSE(r.value().args[0].is_list);
    REQUIRE(r.value().args[0].values.size() == 1);
    CHECK(r.value().args[0].values[0] == "a");
    CHECK(r.value().args[1].name == "from");
    CHECK(r.value().args[1].values[0] == "b");
    CHECK(r.value().args[2].name == "to");
    CHECK(r.value().args[2].values[0] == "c");
}

TEST_CASE("a list value lexes into its elements", "[lexer]")
{
    auto r = lex_token("${string.concat(values=[a, b, c], separator='-')}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 2);
    CHECK(r.value().args[0].name == "values");
    CHECK(r.value().args[0].is_list);
    REQUIRE(r.value().args[0].values.size() == 3);
    CHECK(r.value().args[0].values[0] == "a");
    CHECK(r.value().args[0].values[1] == "b");
    CHECK(r.value().args[0].values[2] == "c");
    CHECK(r.value().args[1].name == "separator");
    CHECK(r.value().args[1].values[0] == "-");
}

TEST_CASE("an empty list value lexes to zero elements", "[lexer]")
{
    auto r = lex_token("${string.concat(values=[])}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 1);
    CHECK(r.value().args[0].is_list);
    CHECK(r.value().args[0].values.empty());
}

TEST_CASE("zero-arg function lexes as a function with no args", "[lexer]")
{
    auto r = lex_token("${gen.next()}");
    REQUIRE(r.has_value());
    CHECK(r.value().is_function);
    CHECK(r.value().args.empty());
}

TEST_CASE("a quoted value strips quotes and protects commas", "[lexer]")
{
    auto r = lex_token("${string.concat(values=['a,b', c])}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 1);
    REQUIRE(r.value().args[0].values.size() == 2);
    CHECK(r.value().args[0].values[0] == "a,b");
    CHECK(r.value().args[0].values[1] == "c");
}

TEST_CASE("the empty quoted value is the empty string", "[lexer]")
{
    auto r = lex_token("${string.replace(value=abc, from=b, to='')}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 3);
    CHECK(r.value().args[2].name == "to");
    REQUIRE(r.value().args[2].values.size() == 1);
    CHECK(r.value().args[2].values[0].empty());
}

TEST_CASE("nested token inside a value is preserved verbatim", "[lexer]")
{
    auto r = lex_token("${string.upper(value=${env.HOME})}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 1);
    REQUIRE(r.value().args[0].values.size() == 1);
    CHECK(r.value().args[0].values[0] == "${env.HOME}");
}

TEST_CASE("interior whitespace survives, boundary whitespace trimmed", "[lexer]")
{
    auto r = lex_token("${string.concat(values=[ a b ,  c ])}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 1);
    REQUIRE(r.value().args[0].values.size() == 2);
    CHECK(r.value().args[0].values[0] == "a b");
    CHECK(r.value().args[0].values[1] == "c");
}

TEST_CASE("malformed tokens report parse_error", "[lexer]")
{
    auto missing_dot = lex_token("${envHOME}");
    REQUIRE_FALSE(missing_dot.has_value());
    CHECK(missing_dot.error().code == resolve_errc::parse_error);

    auto empty_name = lex_token("${env.}");
    REQUIRE_FALSE(empty_name.has_value());

    auto positional = lex_token("${string.upper(abc)}");
    REQUIRE_FALSE(positional.has_value());
    CHECK(positional.error().code == resolve_errc::parse_error);

    auto unbalanced = lex_token("${string.upper(value=a}");
    REQUIRE_FALSE(unbalanced.has_value());

    auto not_a_token = lex_token("plain");
    REQUIRE_FALSE(not_a_token.has_value());

    auto stray = lex_token("${string.upper(value=a)x}");
    REQUIRE_FALSE(stray.has_value());
}

TEST_CASE("a repeated argument name is a parse error", "[lexer]")
{
    // A token call repeating a top-level argument name must be rejected loudly
    // rather than silently keeping the first occurrence and dropping the rest.
    auto duplicate = lex_token("${string.replace(value=a, value=b, to=c)}");
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().code == resolve_errc::parse_error);
    CHECK(duplicate.error().message.find("value") != std::string::npos);
}

TEST_CASE("distinct argument names still lex successfully", "[lexer]")
{
    auto r = lex_token("${string.replace(value=a, from=b, to=c)}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().args.size() == 3);
    CHECK(r.value().args[0].name == "value");
    CHECK(r.value().args[1].name == "from");
    CHECK(r.value().args[2].name == "to");
}

TEST_CASE("a dynamically-named function is a clean parse error", "[lexer]")
{
    // A nested ${...} forming the function name ahead of a top-level '(' is an
    // unsupported nesting shape. It must be a named parse error, not a dispatch
    // of an unresolved literal name.
    auto dynamic_name = lex_token("${cat.${x}(a)}");
    REQUIRE_FALSE(dynamic_name.has_value());
    CHECK(dynamic_name.error().code == resolve_errc::parse_error);
}
