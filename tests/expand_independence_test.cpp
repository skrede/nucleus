// expand() deep-copy independence: a sealed config_space.expand() returns a
// NEW builder pre-populated with a deep copy of the base's registries + ledger.
// Mutating/building the derived builder never affects the base, and a second load
// on the base behaves identically before and after deriving. There is NO shared
// base pointer linking the two (the registries are value-copied).

#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using nucleus::anchor;

namespace {

// One env layer carrying a single (path, value) pair.
nucleus::source_stack env_with(std::string path, std::string text)
{
    nucleus::env_source src;
    src.set(std::move(path), std::move(text));
    return nucleus::source_stack{std::move(src)};
}

}

TEST_CASE("expand() yields an independent builder; the base is unchanged", "[expand][independence]")
{
    // Base: one path-tagged schema registration + one converter (both countable).
    nucleus::config_space_builder base_builder;
    REQUIRE(base_builder.register_schema("port"));
    REQUIRE(base_builder.register_converter<int>(nucleus::make_scalar_converter<int>()));
    nucleus::config_space base = nucleus::builder_result_test::built(base_builder);

    const std::size_t base_schema = base.schema_count();
    const std::size_t base_converters = base.converter_count();

    // Derive a builder, add an ADDITIONAL schema + converter, and build it.
    nucleus::config_space_builder derived_builder = base.expand();
    REQUIRE(derived_builder.register_schema("host"));
    REQUIRE(derived_builder.register_converter<std::int64_t>(
        nucleus::make_scalar_converter<std::int64_t>()));
    nucleus::config_space derived = nucleus::builder_result_test::built(derived_builder);

    // The derived space carries the base's registrations PLUS the new ones.
    REQUIRE(derived.schema_count() == base_schema + 1);
    REQUIRE(derived.converter_count() == base_converters + 1);

    // The base is completely unaffected by the derivation.
    REQUIRE(base.schema_count() == base_schema);
    REQUIRE(base.converter_count() == base_converters);
}

TEST_CASE("deriving does not perturb a later load on the base", "[expand][independence]")
{
    nucleus::config_space_builder base_builder;
    REQUIRE(base_builder.register_element(nucleus::registered_element<int>("port", anchor::root())));
    REQUIRE(base_builder.register_converter<int>(nucleus::make_scalar_converter<int>()));
    nucleus::config_space base = nucleus::builder_result_test::built(base_builder);

    // A load on the base BEFORE deriving.
    auto before = nucleus::load_config(base, env_with("port", "8080"), {});
    REQUIRE(before);
    REQUIRE(before.value().get_as<int>("port"));
    REQUIRE(before.value().get_as<int>("port").value() == 8080);

    // Derive and build a divergent space.
    nucleus::config_space_builder derived_builder = base.expand();
    REQUIRE(derived_builder.register_element(nucleus::registered_element<int>("host", anchor::root())));
    nucleus::config_space derived = nucleus::builder_result_test::built(derived_builder);
    (void)derived;

    // A load on the base AFTER deriving behaves identically -- the derivation did
    // not mutate the base's registries (no shared base pointer).
    auto after = nucleus::load_config(base, env_with("port", "8080"), {});
    REQUIRE(after);
    REQUIRE(after.value().get_as<int>("port"));
    REQUIRE(after.value().get_as<int>("port").value() == 8080);

    // The base still rejects "host" -- the derived builder's extra element never
    // leaked back into the base.
    auto rejects = nucleus::load_config(base, env_with("host", "x"), {});
    REQUIRE_FALSE(rejects);
}

TEST_CASE("a copy of a sealed space is independent of the original", "[expand][independence]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_schema("port"));
    nucleus::config_space original = nucleus::builder_result_test::built(builder);

    // Copy-construct: a deep copy of the value-copyable registries (no shared_ptr
    // base pointer links the two). Distinct objects with equal counts.
    nucleus::config_space copy = original;
    REQUIRE(copy.schema_count() == original.schema_count());
    REQUIRE(&copy != &original);

    // Expanding the copy and adding to it leaves the original untouched.
    nucleus::config_space_builder copy_builder = copy.expand();
    REQUIRE(copy_builder.register_schema("extra"));
    nucleus::config_space grown = nucleus::builder_result_test::built(copy_builder);
    REQUIRE(grown.schema_count() == original.schema_count() + 1);
    REQUIRE(original.schema_count() == 1);
    REQUIRE(copy.schema_count() == 1);
}
