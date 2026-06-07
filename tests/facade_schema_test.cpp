#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/precedence.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/source/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>

// Schema-as-authority over CONTENT, exercised THROUGH the facade: a typed schema
// registered via register_element gates the resolve, rejecting undeclared keys
// (with a nearest-key suggestion) and missing required/identity fields, and
// admitting a valid document.

namespace {

nucleus::key_path path_of(const std::string &text)
{
    auto parsed = nucleus::key_path::parse(text);
    REQUIRE(parsed);
    return parsed.value();
}

nucleus::env_source one(std::string path, std::string text)
{
    nucleus::env_source src;
    src.set(std::move(path), std::move(text));
    return src;
}

} // namespace

TEST_CASE("a typed schema element registers through the facade", "[facade][schema]")
{
    nucleus::configuration_space engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("level", nucleus::anchor::keyspace(path_of("logging")))));

    // Referential integrity is enforced through the facade too: an element under
    // an undefined keyspace is rejected.
    auto bad = engine.register_element(
        nucleus::element("mode", nucleus::anchor::keyspace(path_of("absent"))));
    REQUIRE_FALSE(bad);
}

TEST_CASE("resolve rejects an undeclared key and suggests the nearest declared one",
          "[facade][schema]")
{
    nucleus::configuration_space engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("level", nucleus::anchor::keyspace(path_of("logging")))));

    nucleus::env_source src = one("logging/levle", "debug"); // typo'd key
    nucleus::source_stack stack;
    stack.add(src, nucleus::layer_rank::base, "base");

    auto loaded = engine.resolve(stack);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("not declared") != std::string::npos);
    REQUIRE(loaded.error().find("did you mean") != std::string::npos);
    REQUIRE(loaded.error().find("logging/level") != std::string::npos);
}

TEST_CASE("resolve rejects a missing required field", "[facade][schema]")
{
    nucleus::configuration_space engine;
    REQUIRE(engine.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::required_element("host", nucleus::anchor::keyspace(path_of("server")))));
    REQUIRE(engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace(path_of("server")))));

    // Only the optional port is supplied; the required host is missing.
    nucleus::env_source src = one("server/port", "8080");
    nucleus::source_stack stack;
    stack.add(src, nucleus::layer_rank::base, "base");

    auto loaded = engine.resolve(stack);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("required field 'server/host'") != std::string::npos);
}

TEST_CASE("resolve admits an anonymous strain without the identity field",
          "[facade][schema]")
{
    nucleus::configuration_space engine;
    REQUIRE(engine.register_element(nucleus::element("node", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::identity_element("name", nucleus::anchor::keyspace(path_of("node")))));
    REQUIRE(engine.register_element(
        nucleus::element("role", nucleus::anchor::keyspace(path_of("node")))));

    // A flat source contributing fields without the key is an anonymous strain:
    // it collapses into the configuration space. The primary key is a selector,
    // not a presence obligation.
    nucleus::env_source src = one("node/role", "primary");
    nucleus::source_stack stack;
    stack.add(src, nucleus::layer_rank::base, "base");

    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("node/role") == "primary");
}

TEST_CASE("resolve rejects anonymous-only content when the identity is required",
          "[facade][schema]")
{
    nucleus::configuration_space engine;
    REQUIRE(engine.register_element(nucleus::element("node", nucleus::anchor::root())));
    nucleus::schema_element id =
        nucleus::identity_element("name", nucleus::anchor::keyspace(path_of("node")));
    id.required = true;
    REQUIRE(engine.register_element(std::move(id)));
    REQUIRE(engine.register_element(
        nucleus::element("role", nucleus::anchor::keyspace(path_of("node")))));

    // Requiring the identity element is the host's knob for demanding a NAMED
    // strain; anonymous-only content now fails in required-field vocabulary.
    nucleus::env_source src = one("node/role", "primary");
    nucleus::source_stack stack;
    stack.add(src, nucleus::layer_rank::base, "base");

    auto loaded = engine.resolve(stack);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("required field 'node/name'") != std::string::npos);
}

TEST_CASE("resolve admits a document that satisfies the schema", "[facade][schema]")
{
    nucleus::configuration_space engine;
    REQUIRE(engine.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::required_element("host", nucleus::anchor::keyspace(path_of("server")))));
    REQUIRE(engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace(path_of("server")))));

    nucleus::env_source src;
    src.set("server/host", "localhost").set("server/port", "8080");
    nucleus::source_stack stack;
    stack.add(src, nucleus::layer_rank::base, "base");

    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("server/host") == "localhost");
    REQUIRE(loaded.value().get("server/port") == "8080");
}
