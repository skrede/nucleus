#include "support/builder_result_test_support.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/error.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::runtime_source;
using nucleus::source_stack;

namespace {

nucleus::load_result load_value(const std::string &value)
{
    runtime_source src;
    src.set("a/v", value);
    return load_config(nucleus::builder_result_test::built(config_space_builder{}),
                       source_stack{std::move(src)}, {});
}

std::string result_text(const nucleus::load_result &loaded)
{
    return loaded ? loaded.value().get("a/v").value() : loaded.error().message;
}

nucleus::load_result fallback_load(const std::string &arm)
{
    return load_value("${abs:a/absent ?? " + arm + "}");
}

std::string fallback_value(const std::string &arm)
{
    return result_text(fallback_load(arm));
}

void require_unterminated(const nucleus::load_result &loaded, const std::string &tail)
{
    INFO("result: " << result_text(loaded));
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == nucleus::errc::unresolved_token);
    CHECK(loaded.error().message.ends_with(tail));
}

nucleus::load_result produced_load(const std::string &text)
{
    auto emit = [text](const nucleus::tree_access &) -> nucleus::token_result
    { return text; };

    config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(nucleus::tree_tokenizer("emit", emit)));
    runtime_source src;
    src.set("a/v", "${emit.text}");
    return load_config(nucleus::builder_result_test::built(engine),
                       source_stack{std::move(src)}, {});
}

std::string produced_value(const std::string &text)
{
    return result_text(produced_load(text));
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
    require_unterminated(fallback_load("it's"),
                         "unterminated ${ in value: ' at offset 20 is never closed");
}

TEST_CASE("a tree tokenizer's output with an open apostrophe reports it in the same words",
          "[reference][quoting]")
{
    require_unterminated(produced_load("${abs:x ?? it's}"),
                         "unterminated ${ in value: ' at offset 13 is never closed");
}

TEST_CASE("a double quote left open in a bare fallback arm is named with its offset",
          "[reference][quoting]")
{
    require_unterminated(fallback_load("\"x"),
                         "unterminated ${ in value: \" at offset 18 is never closed");
}

TEST_CASE("an unbalanced brace with no open quote keeps the plain message", "[reference][quoting]")
{
    require_unterminated(produced_load("${abs:x"), "unterminated ${ in value");
}

TEST_CASE("a value missing its closing brace keeps the plain message despite an open quote",
          "[reference][quoting]")
{
    require_unterminated(load_value("${env.HOME is Bob's dir"), "unterminated ${ in value");
    require_unterminated(load_value("${string.upper(value=\"${a'\")}"), "unterminated ${ in value");
}

TEST_CASE("an open quote inside a first-pass function argument is counted from the stripped argument",
          "[reference][quoting]")
{
    require_unterminated(load_value("${string.upper(value=\"${a'}\")}"),
                         "unterminated ${ in value: ' at offset 3 is never closed");
}
