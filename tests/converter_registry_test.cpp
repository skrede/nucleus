// Behavior of the type_index-keyed converter registry as the fourth flat sibling,
// exercised end-to-end through the facade load/resolve so the borrowed-registry
// convert pass is the real path under test:
//   - a registry converter supplies the conversion for a deferred-converter element;
//   - a per-element converter overrides the registry converter for the same element;
//   - an element whose type has no registered converter is left unconverted (no error).

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/converters.h"

#include "nucleus/configuration_source/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <string>

using nucleus::anchor;

TEST_CASE("registry supplies the converter for a deferred-converter element", "[converter][registry]")
{
    nucleus::configuration_space space;
    space.register_converter<int>(nucleus::make_scalar_converter<int>());
    space.register_element(nucleus::registered_element<int>("port", anchor::root()));

    nucleus::env_source env;
    env.set("port", "8080");
    nucleus::configuration_source_stack stack;
    stack.add(env, nucleus::layer_rank::env, "env");

    auto loaded = space.load_configuration(stack);
    REQUIRE(loaded);

    auto typed = loaded.value().get_as<int>("port");
    REQUIRE(typed);
    REQUIRE(typed.value() == 8080);
}

TEST_CASE("a per-element converter overrides the registry converter", "[converter][registry]")
{
    // The registry's converter for int ALWAYS fails; the element carries a working
    // per-element converter. A successful load proves the per-element one was used.
    auto failing = [](std::string_view) -> nucleus::expected<std::any, std::string> {
        return nucleus::unexpected(std::string("registry converter must not run"));
    };

    nucleus::configuration_space space;
    space.register_converter<int>(failing);
    space.register_element(
        nucleus::typed_element<int>("port", anchor::root(), nucleus::make_scalar_converter<int>()));

    nucleus::env_source env;
    env.set("port", "8080");
    nucleus::configuration_source_stack stack;
    stack.add(env, nucleus::layer_rank::env, "env");

    auto loaded = space.load_configuration(stack);
    REQUIRE(loaded);

    auto typed = loaded.value().get_as<int>("port");
    REQUIRE(typed);
    REQUIRE(typed.value() == 8080);
}

TEST_CASE("a type with no registered converter is left unconverted", "[converter][registry]")
{
    // A deferred-converter element whose type has NO registered converter resolves
    // without error: convert() skips it and the raw string remains reachable.
    nucleus::configuration_space space;
    space.register_element(nucleus::registered_element<int>("port", anchor::root()));

    nucleus::env_source env;
    env.set("port", "8080");
    nucleus::configuration_source_stack stack;
    stack.add(env, nucleus::layer_rank::env, "env");

    auto loaded = space.load_configuration(stack);
    REQUIRE(loaded);

    REQUIRE(loaded.value().get("port") == std::string("8080"));
    // No converter ran, so there is no typed value for the path.
    REQUIRE_FALSE(loaded.value().get_as<int>("port"));
}
