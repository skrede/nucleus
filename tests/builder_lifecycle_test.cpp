// Builder lifecycle: build() is infallible and seals into an immutable
// config_space whose counts reflect the registrations; after build() every
// mutating call on the spent builder is a LOUD state-machine error (never a silent
// no-op).

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/tokenizer/tokenizer_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <stdexcept>

using nucleus::anchor;

TEST_CASE("build() is infallible and seals counts into the space", "[builder][lifecycle]")
{
    nucleus::config_space_builder builder;
    const std::size_t builtin_tokenizers = builder.tokenizer_count();
    REQUIRE(builtin_tokenizers >= 2);

    // schema_count() counts path-tagged schema registrations (register_schema).
    REQUIRE(builder.register_schema("server"));
    REQUIRE(builder.register_schema("server/port"));
    REQUIRE(builder.register_tokenizer("custom"));
    REQUIRE(builder.register_converter<int>(nucleus::make_scalar_converter<int>()));

    nucleus::config_space space = builder.build();

    // The sealed space carries exactly what was registered.
    REQUIRE(space.schema_count() == 2);
    REQUIRE(space.tokenizer_count() == builtin_tokenizers + 1);
    REQUIRE(space.converter_count() == 1);
}

TEST_CASE("registering on an already-built builder is a loud state-machine error",
          "[builder][lifecycle]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    nucleus::config_space space = builder.build();
    (void)space;

    // Every mutating call returns an unexpected naming the attempted operation --
    // NOT a silent success.
    auto check = [](const nucleus::registration_result &r, const char *op) {
        REQUIRE_FALSE(r);
        REQUIRE(!r.error().message.empty());
        REQUIRE(r.error().message.find(op) != std::string::npos);
        REQUIRE(r.error().message.find("already been built") != std::string::npos);
    };

    check(builder.register_schema("late"), "register_schema");
    check(builder.register_element(nucleus::element("late", anchor::root())), "register_element");
    check(builder.register_tokenizer("late"), "register_tokenizer");
    check(builder.register_converter<std::int64_t>(
              nucleus::make_scalar_converter<std::int64_t>()), "register_converter");
    check(builder.install_tokenizer(nucleus::tokenizer_builder("late").build()),
          "install_tokenizer");
    check(builder.set_registration_policy(nullptr), "set_registration_policy");
}

TEST_CASE("name() and a second build() on a spent builder throw loudly",
          "[builder][lifecycle]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    nucleus::config_space space = builder.build();
    (void)space;

    // These doors return config_space_builder& / config_space, not an expected, so
    // the loud channel is a throw -- REQUIRE_THROWS_AS, not REQUIRE_FALSE.
    REQUIRE_THROWS_AS(builder.name("late"), std::invalid_argument);
    REQUIRE_THROWS_AS(builder.build(), std::invalid_argument);
}

TEST_CASE("config_space_builder move transfers builder state to the moved-to builder",
          "[builder][lifecycle]")
{
    // Move-construct: the moved-to builder owns the registrations and seals them.
    nucleus::config_space_builder source;
    REQUIRE(source.register_schema("server"));
    REQUIRE(source.register_schema("server/port"));

    nucleus::config_space_builder moved_ctor(std::move(source));
    REQUIRE(moved_ctor.schema_count() == 2);
    nucleus::config_space from_ctor = moved_ctor.build();
    REQUIRE(from_ctor.schema_count() == 2);

    // Move-assign: the moved-to builder likewise carries the source's state.
    nucleus::config_space_builder assign_source;
    REQUIRE(assign_source.register_schema("client"));

    nucleus::config_space_builder moved_assign;
    moved_assign = std::move(assign_source);
    REQUIRE(moved_assign.schema_count() == 1);
    nucleus::config_space from_assign = moved_assign.build();
    REQUIRE(from_assign.schema_count() == 1);
}
