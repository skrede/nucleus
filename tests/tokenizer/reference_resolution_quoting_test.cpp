#include "support/builder_result_test_support.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::runtime_source;
using nucleus::source_stack;

namespace {

std::string fallback_value(const std::string &arm)
{
    runtime_source src;
    src.set("a/v", "${abs:a/absent ?? " + arm + "}");
    auto loaded = load_config(nucleus::builder_result_test::built(config_space_builder{}),
                              source_stack{std::move(src)}, {});
    return loaded ? loaded.value().get("a/v").value() : loaded.error().message;
}

std::string produced_value(const std::string &text)
{
    auto emit = [text](const nucleus::tree_access &) -> nucleus::token_result
    { return text; };

    config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(nucleus::tree_tokenizer("emit", emit)));
    runtime_source src;
    src.set("a/v", "${emit.text}");
    auto loaded = load_config(nucleus::builder_result_test::built(engine),
                              source_stack{std::move(src)}, {});
    return loaded ? loaded.value().get("a/v").value() : loaded.error().message;
}

}

TEST_CASE("a closing brace inside a quoted fallback arm does not end the tree token",
          "[reference][quoting]")
{
    auto           space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/alias", "${abs:cluster/absent ?? \"}\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias").value() == "}");
}

TEST_CASE("a fallback arm in single quotes is the same literal as one in double quotes",
          "[reference][quoting]")
{
    CHECK(fallback_value("'}'") == "}");
    CHECK(fallback_value("\"}\"") == "}");
    CHECK(fallback_value("'x'") == "x");
    CHECK(fallback_value("\"x\"") == "x");
    CHECK(fallback_value("'1.5'") == "1.5");
    CHECK(fallback_value("\"1.5\"") == "1.5");
    CHECK(fallback_value("'it\"s'") == "it\"s");
    CHECK(fallback_value("\"it's\"") == "it's");
    CHECK(fallback_value("'a}' ?? \"z\"") == "a}");
    CHECK(fallback_value("\"a\" \"b\"") == "\"a\" \"b\"");
}

TEST_CASE("a resolver's output quoted in either quote is a literal",
          "[reference][quoting][produced]")
{
    CHECK(produced_value("${'x'}") == "x");
    CHECK(produced_value("${\"x\"}") == "x");
    CHECK(produced_value("${'a.b'}") == "a.b");
    CHECK(produced_value("${\"a.b\"}") == "a.b");
    CHECK(produced_value("${\"${HOME}/.config\"}") == "${HOME}/.config");
    CHECK(produced_value("${\"a\" \"b\"}").find("unrecognized token body") != std::string::npos);
}

TEST_CASE("a ?? inside a quoted arm does not split it, in either quote", "[reference][quoting]")
{
    CHECK(fallback_value("'a??b'") == "a??b");
    CHECK(fallback_value("\"a??b\"") == "a??b");
    CHECK(fallback_value("'it\"s' ?? 'x'") == "it\"s");
    CHECK(fallback_value("\"it's\" ?? \"x\"") == "it's");
    CHECK(fallback_value("a???b") == "a");
}

TEST_CASE("an apostrophe left open in a bare fallback arm is named with its offset",
          "[reference][quoting]")
{
    std::string const message = fallback_value("it's");
    INFO(message);
    CHECK(message.find("unterminated ${ in value: ' at offset 20 is never closed") != std::string::npos);
}

TEST_CASE("a tree tokenizer's output with an open apostrophe reports it in the same words",
          "[reference][quoting]")
{
    std::string const message = produced_value("${abs:x ?? it's}");
    INFO(message);
    CHECK(message.find("unterminated ${ in value: ' at offset 13 is never closed") != std::string::npos);
}

TEST_CASE("a double quote left open in a bare fallback arm is named with its offset",
          "[reference][quoting]")
{
    std::string const message = fallback_value("\"x");
    INFO(message);
    CHECK(message.find("unterminated ${ in value: \" at offset 18 is never closed") != std::string::npos);
}

TEST_CASE("an unbalanced brace with no open quote keeps the plain message", "[reference][quoting]")
{
    std::string const message = produced_value("${abs:x");
    INFO(message);
    CHECK(message.find("unterminated ${ in value") != std::string::npos);
    CHECK(message.find("never closed") == std::string::npos);
}
