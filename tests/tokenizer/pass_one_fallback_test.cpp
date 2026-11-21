#include "support/builder_result_test_support.h"

#include "nucleus/error.h"
#include "nucleus/identity.h"
#include "nucleus/config_space.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/runtime/runtime_source.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/tree_tokenizer.h"
#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <string_view>

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::owner_token;
using nucleus::resolve_errc;
using nucleus::resolve_tokens;
using nucleus::runtime_source;
using nucleus::source_stack;
using nucleus::tokenizer_registry;

namespace {

constexpr std::string_view pass_two_operator = "?? is the tree-reference (pass-2) fallback operator";

tokenizer_registry core_registry()
{
    tokenizer_registry r;
    r.add(nucleus::make_env_tokenizer(), owner_token{});
    r.add(nucleus::make_string_tokenizer(), owner_token{});
    return r;
}

std::string resolved(std::string_view text)
{
    auto r = resolve_tokens(text, core_registry());
    return r ? r.value() : r.error().message;
}

void check_refused(std::string_view text)
{
    INFO("input: " << text);
    auto r = resolve_tokens(text, core_registry());
    REQUIRE_FALSE(r.has_value());
    INFO(nucleus::to_string(r.error().code) << ": " << r.error().message);
    CHECK(r.error().code == resolve_errc::parse_error);
    CHECK(r.error().message.find(pass_two_operator) != std::string::npos);
}

nucleus::load_result load_value(config_space_builder &engine, std::string const &value)
{
    runtime_source src;
    src.set("a/v", value);
    return load_config(nucleus::builder_result_test::built(engine), source_stack{std::move(src)}, {});
}

}

TEST_CASE("a top-level ?? in a pass-1 token is a parse error naming the pass-2 operator", "[resolve][fallback]")
{
    check_refused("${env.NUCLEUS_NOPE ?? \"x\"}");
    check_refused("${string.upper(value=a) ?? \"x\"}");
    check_refused("${\"x\" ?? y}");
}

TEST_CASE("a ?? beside a nested head is refused before the head is resolved", "[resolve][fallback]")
{
    check_refused("${env.${env.NUCLEUS_NOPE_INNER} ?? x}");
}

TEST_CASE("a ?? inside an argument list, a quoted run or a nested token is not the outer token's operator",
          "[resolve][fallback]")
{
    CHECK(resolved("${string.upper(value=a??b)}") == "A??B");
    CHECK(resolved("${string.upper(value='a??b')}") == "A??B");
    CHECK(resolved("${string.concat(values=[${abs:a ?? \"z\"}])}") == "${abs:a ?? \"z\"}");
    CHECK(resolved("${string.upper(value=a)}??x") == "A??x");
}

TEST_CASE("a load holding a pass-1 ?? fails with unresolved_token naming the pass-2 operator", "[resolve][fallback]")
{
    config_space_builder engine;
    auto                 loaded = load_value(engine, "${env.NUCLEUS_NOPE ?? \"x\"}");
    REQUIRE_FALSE(loaded.has_value());
    INFO(loaded.error().message);
    CHECK(loaded.error().code == nucleus::errc::unresolved_token);
    CHECK(loaded.error().message.find(pass_two_operator) != std::string::npos);
}

TEST_CASE("a tree tokenizer token keeps its ?? for the reference pass", "[resolve][fallback]")
{
    auto emit = [](const nucleus::tree_access &) -> nucleus::token_result
    { return "emitted"; };

    config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(nucleus::tree_tokenizer("emit", emit)));
    auto loaded = load_value(engine, "${emit.f ?? \"x\"}");
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("a/v").value() == "emitted");
}
