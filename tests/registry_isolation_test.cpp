#include "nucleus/identity.h"

#include "nucleus/registry/flat_registry.h"

#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"
#include "nucleus/schema/converters.h"

#include "nucleus/configuration_source/configuration_source_registry.h"

#include "nucleus/entry/resolution_context.h"

#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Compile-time enforcement: each registry must be constructible with only its
// own dependencies, independently of every sibling. A registry that held a
// sibling reference member would not be default-constructible and would fail
// these assertions, stopping the build. (The negative fixtures prove the
// converse explicitly.)
static_assert(nucleus::flat_registry<nucleus::schema_registry>);
static_assert(nucleus::flat_registry<nucleus::tokenizer_registry>);
static_assert(nucleus::flat_registry<nucleus::configuration_source_registry>);
static_assert(nucleus::flat_registry<nucleus::converter_registry>);

// Strengthened pin: each registry is independently constructible AND exposes no
// constructor that takes any of its siblings by reference or pointer. Naming the
// three siblings of each registry makes the entangling-constructor check span the
// whole flat set of four registries.
static_assert(nucleus::independently_constructible<
              nucleus::schema_registry,
              nucleus::tokenizer_registry,
              nucleus::configuration_source_registry,
              nucleus::converter_registry>::value);
static_assert(nucleus::independently_constructible<
              nucleus::tokenizer_registry,
              nucleus::schema_registry,
              nucleus::configuration_source_registry,
              nucleus::converter_registry>::value);
static_assert(nucleus::independently_constructible<
              nucleus::configuration_source_registry,
              nucleus::schema_registry,
              nucleus::tokenizer_registry,
              nucleus::converter_registry>::value);
static_assert(nucleus::independently_constructible<
              nucleus::converter_registry,
              nucleus::schema_registry,
              nucleus::tokenizer_registry,
              nucleus::configuration_source_registry>::value);

TEST_CASE("each registry is constructed and exercised with no sibling in scope", "[isolation]")
{
    SECTION("schema registry alone")
    {
        nucleus::schema_registry schema;
        schema.add(nucleus::schema_spec{"a/b"}, nucleus::owner_token(std::string("o")));
        REQUIRE(schema.size() == 1);
    }

    SECTION("tokenizer registry alone")
    {
        nucleus::tokenizer_registry tokenizer;
        tokenizer.add(nucleus::tokenizer_builder("env").build(), nucleus::owner_token{});
        REQUIRE(tokenizer.size() == 1);
    }

    SECTION("source registry alone")
    {
        nucleus::configuration_source_registry sources;
        sources.add(nucleus::configuration_source_spec{"argv"}, nucleus::owner_token{});
        REQUIRE(sources.size() == 1);
    }

    SECTION("converter registry alone")
    {
        nucleus::converter_registry converters;
        converters.set<int>(nucleus::make_scalar_converter<int>());
        REQUIRE(converters.size() == 1);
    }
}

TEST_CASE("siblings collaborate only through a hand-built resolution context", "[isolation]")
{
    // The registries are built independently and only meet through the transient
    // context, which borrows the ones it consults. No registry references another.
    // The context borrows the schema and tokenizer registries it reads during a
    // resolve; the source registry remains a flat sibling, built and exercised on
    // its own (sources to fold arrive through the precedence stack, not the
    // registry), which is precisely the point of the flat topology.
    nucleus::schema_registry schema;
    nucleus::tokenizer_registry tokenizer;
    nucleus::configuration_source_registry sources;
    nucleus::converter_registry converters;

    nucleus::resolution_context ctx(schema, tokenizer, converters);

    ctx.schema().add(nucleus::schema_spec{"k"}, nucleus::owner_token{});
    ctx.tokenizer().add(nucleus::tokenizer_builder("noop").build(), nucleus::owner_token{});
    ctx.converters().set<int>(nucleus::make_scalar_converter<int>());
    sources.add(nucleus::configuration_source_spec{"env"}, nucleus::owner_token{});

    REQUIRE(schema.size() == 1);
    REQUIRE(tokenizer.size() == 1);
    REQUIRE(sources.size() == 1);
    REQUIRE(converters.size() == 1);
}
