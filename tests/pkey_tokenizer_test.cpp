#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>

using nucleus::anchor;
using nucleus::config_space_builder;
using nucleus::key_path;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::resolve_errc;
using nucleus::runtime_source;
using nucleus::source_stack;
using nucleus::token_result;
using nucleus::tree_access;
using nucleus::tree_tokenizer;

namespace {

void declare_server_schema(config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
            nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
            nucleus::element("port", anchor::keyspace("cluster/server"))));
}

void declare_nested_field_schema(config_space_builder &engine)
{
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
            nucleus::element("endpoint", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
            nucleus::element("secret", anchor::keyspace("cluster/server/endpoint"))));
    REQUIRE(engine.register_element(
            nucleus::element("desc", anchor::keyspace("cluster/server"))));
}

nucleus::load_result load_nested_field(bool host, std::size_t &calls)
{
    config_space_builder engine;
    declare_nested_field_schema(engine);
    if(host)
        REQUIRE(engine.install_tree_tokenizer(tree_tokenizer(
                "server", [&calls](const tree_access &) -> token_result
                {
                    ++calls;
                    return std::string("dispatched"); })));
    const auto     space = nucleus::builder_result_test::built(engine);
    runtime_source source;
    source.set("cluster/server/alpha/name", "alpha")
            .set("cluster/server/alpha/endpoint/secret", "hidden")
            .set("cluster/server/alpha/desc", "${server.endpoint/secret}");
    load_options options;
    options.selection = "alpha";
    return load_config(space, source_stack{std::move(source)}, options);
}

}

TEST_CASE("auto-named tokenizer resolves pkey field", "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
            nucleus::element("desc", anchor::keyspace("cluster/server"))));
    auto           space = nucleus::builder_result_test::built(engine);
    runtime_source src;
    src.set("cluster/server/primary/name", "primary")
            .set("cluster/server/primary/port", "8080")
            .set("cluster/server/primary/desc", "${server.name}:${server.port}");

    load_options opts;
    opts.selection = "primary";
    auto loaded    = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/desc") == "primary:8080");
}

TEST_CASE("auto-named tokenizer: same-tag nested instances are unambiguous",
          "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
            nucleus::element("label", anchor::keyspace("cluster/server"))));
    auto           space = nucleus::builder_result_test::built(engine);
    runtime_source src;
    src.set("cluster/server/primary/name", "primary")
            .set("cluster/server/primary/port", "9000")
            .set("cluster/server/primary/label", "${server.name}")
            .set("cluster/server/secondary/name", "secondary")
            .set("cluster/server/secondary/port", "9001")
            .set("cluster/server/secondary/label", "${server.name}");

    load_options opts;
    opts.selection = "secondary";
    auto loaded    = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/label") == "secondary");
}

TEST_CASE("Zero-instance: precise diagnostic when no selection", "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(nucleus::element("app", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("desc", anchor::keyspace("app"))));
    auto           space = nucleus::builder_result_test::built(engine);
    runtime_source src;
    src.set("app/desc", "${server.name ?? \"default\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("app/desc") == "default");
}

TEST_CASE("Zero-instance: hard error without fallback", "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(nucleus::element("app", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("desc", anchor::keyspace("app"))));
    auto space = nucleus::builder_result_test::built(engine);

    runtime_source src;
    src.set("app/desc", "${server.name}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("requires a selected primary-key instance") != std::string::npos);
}

TEST_CASE("Reserved-name rejection via register_element", "[pkey_tokenizer]")
{
    const std::vector<std::string> reserved =
            {"env", "string", "abs", "rel", "scope", "file", "dir", "self"};

    for(const std::string &name : reserved)
    {
        config_space_builder engine;
        REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
        REQUIRE(engine.register_element(
                nucleus::element(name, anchor::keyspace("cluster"))));
        auto result = engine.register_element(
                nucleus::primary_key_element("id", anchor::keyspace("cluster/" + name)));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == nucleus::errc::rejected_registration);
        CHECK(result.error().message.find("collides with a reserved name") != std::string::npos);
    }
}

TEST_CASE("Host shadow wins over auto-registered pkey tokenizer", "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
            nucleus::element("desc", anchor::keyspace("cluster/server"))));

    auto host_result = engine.install_tree_tokenizer(
            tree_tokenizer("server",
                           [](const tree_access &) -> token_result
                           { return std::string("host-value"); }));
    REQUIRE(host_result.has_value());

    auto space = nucleus::builder_result_test::built(engine);

    runtime_source src;
    src.set("cluster/server/primary/name", "primary")
            .set("cluster/server/primary/desc", "${server.name}");

    load_options opts;
    opts.selection = "primary";
    auto loaded    = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/desc") == "host-value");
}

TEST_CASE("tree tokenizer direct access rejects nested field syntax",
          "[pkey_tokenizer]")
{
    std::size_t       calls = 0;
    tree_tokenizer    tokenizer("server", [&calls](const tree_access &) -> token_result
                                {
        ++calls;
        return std::string("dispatched"); });
    nucleus::keyspace building;
    const key_path    current_path = key_path::parse("cluster/server/desc").value();
    const tree_access access{building, current_path, "server", "endpoint/secret"};
    const auto        result = tokenizer.resolve(access);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == resolve_errc::missing_field);
    CHECK(result.error().message.find("one direct child segment") != std::string::npos);
    CHECK(calls == 0);
}

TEST_CASE("tree tokenizer loads reject nested field syntax before lookup",
          "[pkey_tokenizer]")
{
    std::size_t calls = 0;
    for(const bool host : {false, true})
    {
        DYNAMIC_SECTION((host ? "host" : "built-in"))
        {
            const auto loaded = load_nested_field(host, calls);
            REQUIRE_FALSE(loaded.has_value());
            CHECK(loaded.error().code == nucleus::errc::unresolved_token);
            CHECK(loaded.error().message.find("endpoint/secret") != std::string::npos);
            CHECK(loaded.error().message.find("one direct child segment") != std::string::npos);
            CHECK(calls == 0);
        }
    }
}
