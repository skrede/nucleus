#include "nucleus/identity.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstdlib>
#include <unordered_map>

using nucleus::owner_token;
using nucleus::resolve_errc;
using nucleus::resolver_scope;
using nucleus::resolve_tokens;
using nucleus::tokenizer_builder;
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

TEST_CASE("literal text with no token passes through", "[resolve]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("nothing here", reg);
    REQUIRE(r.has_value());
    CHECK(r.value() == "nothing here");
}

TEST_CASE("env tokenizer expands a set variable and fails on unset", "[resolve][env]")
{
#ifdef _WIN32
    _putenv_s("NUCLEUS_TEST_VAR", "expanded");
#else
    setenv("NUCLEUS_TEST_VAR", "expanded", 1);
#endif
    auto reg = core_registry();
    auto ok = resolve_tokens("v=${env.NUCLEUS_TEST_VAR}!", reg);
    REQUIRE(ok.has_value());
    CHECK(ok.value() == "v=expanded!");

    auto miss = resolve_tokens("${env.NUCLEUS_DEFINITELY_UNSET_XYZ}", reg);
    REQUIRE_FALSE(miss.has_value());
    CHECK(miss.error().code == resolve_errc::missing_field);
}

TEST_CASE("string tokenizer ops", "[resolve][string]")
{
    auto reg = core_registry();
    CHECK(resolve_tokens("${string.upper(abc)}", reg).value() == "ABC");
    CHECK(resolve_tokens("${string.lower(ABC)}", reg).value() == "abc");
    CHECK(resolve_tokens("${string.trim(  x  )}", reg).value() == "x");
    CHECK(resolve_tokens("${string.length(hello)}", reg).value() == "5");
    CHECK(resolve_tokens("${string.replace(a.b.c, ., /)}", reg).value() == "a/b/c");
    CHECK(resolve_tokens("${string.concat(a, b, c)}", reg).value() == "abc");
    CHECK(resolve_tokens("${string.substr(hello, 1)}", reg).value() == "ello");
    CHECK(resolve_tokens("${string.substr(hello, 1, 3)}", reg).value() == "ell");
}

TEST_CASE("unknown category is a named error", "[resolve]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("${nope.x}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::unknown_category);
}

TEST_CASE("nested function-arg tokens resolve inner first", "[resolve][nested]")
{
#ifdef _WIN32
    _putenv_s("NUCLEUS_NEST", "abc");
#else
    setenv("NUCLEUS_NEST", "abc", 1);
#endif
    auto reg = core_registry();
    auto r = resolve_tokens("${string.upper(${env.NUCLEUS_NEST})}", reg);
    REQUIRE(r.has_value());
    CHECK(r.value() == "ABC");
}

TEST_CASE("field-form nesting resolves the head to a fixpoint", "[resolve][nested]")
{
#ifdef _WIN32
    _putenv_s("PICK", "REAL");
    _putenv_s("REAL", "value");
#else
    setenv("PICK", "REAL", 1);
    setenv("REAL", "value", 1);
#endif
    auto reg = core_registry();
    // ${env.${env.PICK}} -> ${env.REAL} -> value
    auto r = resolve_tokens("${env.${env.PICK}}", reg);
    REQUIRE(r.has_value());
    CHECK(r.value() == "value");
}

TEST_CASE("scope file-frame keys resolve against the file frame", "[resolve][scope]")
{
    auto reg = core_registry();
    resolver_scope scope(reg);
    auto frame = scope.push_file_frame("/etc/app/config.xml");

    CHECK(scope.resolve_all("${scope.file_name}").value() == "config.xml");
    CHECK(scope.resolve_all("${scope.file_stem}").value() == "config");
    CHECK(scope.resolve_all("${scope.file_directory}").value() == "/etc/app");
    CHECK(scope.resolve_all("${scope.file_path}").value() == "/etc/app/config.xml");
}

TEST_CASE("file/dir/self location categories resolve against the file frame", "[resolve][scope]")
{
    auto reg = core_registry();
    resolver_scope scope(reg);
    auto frame = scope.push_file_frame("/etc/app/config.xml");

    CHECK(scope.resolve_all("${file.name}").value() == "config.xml");
    CHECK(scope.resolve_all("${file.stem}").value() == "config");
    CHECK(scope.resolve_all("${dir.path}").value() == "/etc/app");
    CHECK(scope.resolve_all("${dir.name}").value() == "app");
    CHECK(scope.resolve_all("${self.path}").value() == "/etc/app/config.xml");
}

TEST_CASE("scope key with no file frame is out_of_scope_context", "[resolve][scope]")
{
    auto reg = core_registry();
    resolver_scope scope(reg);
    auto r = scope.resolve_all("${scope.file_name}");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::out_of_scope_context);
}

TEST_CASE("file-frame convenience entry point activates scope keys", "[resolve][scope]")
{
    auto reg = core_registry();
    auto r = resolve_tokens("${scope.file_stem}.bak", reg, "/data/thing.conf");
    REQUIRE(r.has_value());
    CHECK(r.value() == "thing.bak");
}

TEST_CASE("host-registered generic frame category resolves its bindings", "[resolve][scope][host]")
{
    auto reg = core_registry();
    resolver_scope scope(reg);
    std::unordered_map<std::string, std::string> bindings{{"name", "edge-1"}, {"region", "eu"}};
    auto frame = scope.push_scope_frame("node", std::move(bindings));

    CHECK(scope.resolve_all("${node.name}@${node.region}").value() == "edge-1@eu");

    auto miss = scope.resolve_all("${node.unknown}");
    REQUIRE_FALSE(miss.has_value());
}
