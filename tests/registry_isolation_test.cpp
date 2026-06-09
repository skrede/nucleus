#include "nucleus/identity.h"

#include "nucleus/registry/flat_registry.h"

#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"
#include "nucleus/schema/converters.h"

#include "nucleus/entry/resolution_context.h"

#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Compile-time enforcement: each registry must be constructible with only its
// own dependencies, independently of every sibling. A registry that held a
// sibling reference member would not be default-constructible and would fail
// these assertions, stopping the build.
static_assert(nucleus::flat_registry<nucleus::schema_registry>);
static_assert(nucleus::flat_registry<nucleus::tokenizer_registry>);
static_assert(nucleus::flat_registry<nucleus::converter_registry>);

// Strengthened pin: each registry is independently constructible AND exposes no
// constructor that takes any of its siblings by reference or pointer.
static_assert(nucleus::independently_constructible<
              nucleus::schema_registry,
              nucleus::tokenizer_registry,
              nucleus::converter_registry>::value);
static_assert(nucleus::independently_constructible<
              nucleus::tokenizer_registry,
              nucleus::schema_registry,
              nucleus::converter_registry>::value);
static_assert(nucleus::independently_constructible<
              nucleus::converter_registry,
              nucleus::schema_registry,
              nucleus::tokenizer_registry>::value);

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
    nucleus::schema_registry schema;
    nucleus::tokenizer_registry tokenizer;
    nucleus::converter_registry converters;

    // Populate the registries directly; the context borrows them by CONST reference
    // (read-only) and exposes them through const accessors.
    schema.add(nucleus::schema_spec{"k"}, nucleus::owner_token{});
    tokenizer.add(nucleus::tokenizer_builder("noop").build(), nucleus::owner_token{});
    converters.set<int>(nucleus::make_scalar_converter<int>());

    nucleus::resolution_context ctx(schema, tokenizer, converters);

    REQUIRE(ctx.schema().size() == 1);
    REQUIRE(ctx.tokenizer().size() == 1);
    REQUIRE(ctx.converters().size() == 1);
}
