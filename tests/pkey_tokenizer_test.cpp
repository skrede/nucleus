#include "nucleus/config_space.h"
#include "nucleus/format.h"
#include "nucleus/identity.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Acceptance tests.
// All tests drive load_config() with runtime_source so the full fold -> slice ->
// resolve_references() -> validate() -> freeze() chain runs.

using nucleus::anchor;
using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::runtime_source;
using nucleus::source_stack;
using nucleus::tree_tokenizer;
using nucleus::tree_access;
using nucleus::token_result;
using nucleus::unexpected;
using nucleus::resolve_error;
using nucleus::resolve_errc;
using nucleus::key_path;

namespace {

// Registers cluster/server schema with identity "name" and sibling leaf "port".
void declare_server_schema(config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/server"))));
}

}

TEST_CASE("auto-named tokenizer resolves pkey field", "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
        nucleus::element("desc", anchor::keyspace("cluster/server"))));
    auto space = engine.build();

    runtime_source src;
    src.set("cluster/server/primary/name", "primary")
       .set("cluster/server/primary/port", "8080")
       .set("cluster/server/primary/desc", "${server.name}:${server.port}");

    load_options opts;
    opts.selection = "primary";
    auto loaded = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/desc") == "primary:8080");
}

TEST_CASE("auto-named tokenizer: same-tag nested instances are unambiguous",
          "[pkey_tokenizer]")
{
    // Two strains; selecting one must not leak the other's field into a pkey token.
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
        nucleus::element("label", anchor::keyspace("cluster/server"))));
    auto space = engine.build();

    runtime_source src;
    src.set("cluster/server/primary/name",  "primary")
       .set("cluster/server/primary/port",  "9000")
       .set("cluster/server/primary/label", "${server.name}")
       .set("cluster/server/secondary/name",  "secondary")
       .set("cluster/server/secondary/port",  "9001")
       .set("cluster/server/secondary/label", "${server.name}");

    // Select "secondary" — ${server.name} must resolve to "secondary", not "primary".
    load_options opts;
    opts.selection = "secondary";
    auto loaded = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/label") == "secondary");
}

TEST_CASE("Zero-instance: precise diagnostic when no selection", "[pkey_tokenizer]")
{
    // Put the pkey token in a non-keyed path (app/desc) so the token is always
    // evaluated post-slice, even when the keyed container has zero instances.
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(nucleus::element("app", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("desc", anchor::keyspace("app"))));
    auto space = engine.build();

    // Supply NO server instances. The ${server.name} in app/desc is evaluated
    // post-slice but the identity leaf is absent — the diagnostic fires.
    // The ?? fallback must resolve to "default" when no instance is in scope.
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
    auto space = engine.build();

    // No server instances; token ${server.name} fails with a precise diagnostic.
    runtime_source src;
    src.set("app/desc", "${server.name}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("requires a selected primary-key instance")
          != std::string::npos);
}

TEST_CASE("Reserved-name rejection via register_element", "[pkey_tokenizer]")
{
    // Each reserved category name must produce errc::rejected_registration.
    const std::vector<std::string> reserved =
        {"env", "string", "abs", "rel", "scope", "file", "dir", "self"};

    for(const std::string &name : reserved)
    {
        config_space_builder engine;
        REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
        // Register a container whose last segment IS the reserved name.
        REQUIRE(engine.register_element(
            nucleus::element(name, anchor::keyspace("cluster"))));
        // Register an identity element under cluster/<reserved_name>:
        // the container tag = reserved name → rejection must fire.
        auto result = engine.register_element(
            nucleus::primary_key_element("id", anchor::keyspace("cluster/" + name)));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == nucleus::errc::rejected_registration);
        CHECK(result.error().message.find("collides with a reserved name")
              != std::string::npos);
    }
}

TEST_CASE("Host shadow wins over auto-registered pkey tokenizer", "[pkey_tokenizer]")
{
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
        nucleus::element("desc", anchor::keyspace("cluster/server"))));

    // Install a host tree tokenizer for category "server" that always returns "host-value".
    auto host_result = engine.install_tree_tokenizer(
        tree_tokenizer("server",
            [](const tree_access &) -> token_result { return std::string("host-value"); }));
    REQUIRE(host_result.has_value());

    auto space = engine.build();

    runtime_source src;
    src.set("cluster/server/primary/name", "primary")
       .set("cluster/server/primary/desc", "${server.name}");

    load_options opts;
    opts.selection = "primary";
    auto loaded = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    // Host-registered tokenizer wins over the built-in auto-named one.
    CHECK(loaded.value().get("cluster/server/desc") == "host-value");
}

TEST_CASE("Host-defined tree tokenizer equivalent to built-in",
          "[pkey_tokenizer]")
{
    // Build a schema with identity "name" under "cluster/server".
    // Install a HOST tree tokenizer that reads cluster/server/name directly,
    // replicating the built-in auto-named tokenizer's behavior.
    config_space_builder engine;
    declare_server_schema(engine);
    REQUIRE(engine.register_element(
        nucleus::element("addr", anchor::keyspace("cluster/server"))));

    // Host tokenizer reads the field from the sliced keyspace, mirroring the built-in.
    auto host_result = engine.install_tree_tokenizer(
        tree_tokenizer("server",
            [](const tree_access &access) -> token_result
            {
                // Read cluster/server/<field_name> from the assembled tree.
                auto field_path = key_path::parse(
                    "cluster/server/" + std::string(access.field_name));
                if(!field_path)
                    return unexpected(resolve_error(resolve_errc::missing_field,
                        "invalid field path"));
                const nucleus::value *v = access.building.find(field_path.value());
                if(v == nullptr)
                    return unexpected(resolve_error(resolve_errc::missing_field,
                        nucleus::format("${{server.{}}} not found", access.field_name)));
                return std::string(v->text());
            }));
    REQUIRE(host_result.has_value());

    auto space = engine.build();

    runtime_source src;
    src.set("cluster/server/alpha/name", "alpha")
       .set("cluster/server/alpha/addr", "${server.name}");

    load_options opts;
    opts.selection = "alpha";
    auto loaded = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE(loaded.has_value());
    // Host-defined tokenizer resolves ${server.name} identically to the built-in.
    CHECK(loaded.value().get("cluster/server/addr") == "alpha");
}
