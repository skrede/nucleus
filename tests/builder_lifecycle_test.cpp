// Builder lifecycle: build() is infallible and seals into an immutable
// configuration_space whose counts reflect the registrations; after build() every
// mutating call on the spent builder is a LOUD state-machine error (never a silent
// no-op).

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/tokenizer/tokenizer_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using nucleus::anchor;

TEST_CASE("build() is infallible and seals counts into the space", "[builder][lifecycle]")
{
    nucleus::configuration_space_builder builder;
    const std::size_t builtin_tokenizers = builder.tokenizer_count();
    REQUIRE(builtin_tokenizers >= 2);

    // schema_count() counts path-tagged schema registrations (register_schema).
    REQUIRE(builder.register_schema("server"));
    REQUIRE(builder.register_schema("server/port"));
    REQUIRE(builder.register_source("argv"));
    REQUIRE(builder.register_converter<int>(nucleus::make_scalar_converter<int>()));

    nucleus::configuration_space space = builder.build();

    // The sealed space carries exactly what was registered.
    REQUIRE(space.schema_count() == 2);
    REQUIRE(space.tokenizer_count() == builtin_tokenizers);
    REQUIRE(space.source_count() == 1);
    REQUIRE(space.converter_count() == 1);
}

TEST_CASE("registering on an already-built builder is a loud state-machine error",
          "[builder][lifecycle]")
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", anchor::root()));
    nucleus::configuration_space space = builder.build();
    (void)space;

    // Every mutating call returns an unexpected naming the attempted operation --
    // NOT a silent success.
    auto check = [](const nucleus::registration_result &r, const char *op) {
        REQUIRE_FALSE(r);
        REQUIRE(!r.error().empty());
        REQUIRE(r.error().find(op) != std::string::npos);
        REQUIRE(r.error().find("already been built") != std::string::npos);
    };

    check(builder.register_schema("late"), "register_schema");
    check(builder.register_element(nucleus::element("late", anchor::root())), "register_element");
    check(builder.register_tokenizer("late"), "register_tokenizer");
    check(builder.register_source("late"), "register_source");
    check(builder.register_converter<std::int64_t>(
              nucleus::make_scalar_converter<std::int64_t>()), "register_converter");
    check(builder.install_tokenizer(nucleus::tokenizer_builder("late").build()),
          "install_tokenizer");
    check(builder.set_registration_policy(nullptr), "set_registration_policy");
}
