#include "nucleus/capability.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/entry/precedence.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

// A stub source declaring a capable descriptor (nesting/duplicate_keys/typed),
// used as a custom base layer to prove a single capable layer satisfies a HARD
// requirement that the always-present flat layers cannot (whole-stack union).
class capable_source final : public nucleus::configuration_source
{
public:
    [[nodiscard]] nucleus::capability_descriptor capabilities() const override
    {
        return nucleus::capability_descriptor{nucleus::capability::nesting,
                                              nucleus::capability::duplicate_keys,
                                              nucleus::capability::typed_scalars};
    }

    [[nodiscard]] nucleus::configuration_source_result pull() override
    {
        return nucleus::configuration_source_batch{};
    }
};

// A sealed space whose schema is nested (a `server` container primary-keyed by
// `name`) and typed (an int `port` leaf) -- so it derives a HARD nesting
// requirement and a SOFT typed_scalars one. Authoring order is fixed by attach
// referential integrity, but the derived requirement set is order-independent.
[[nodiscard]] nucleus::configuration_space make_nested_typed_space()
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(
        nucleus::typed_element<int>("port", nucleus::anchor::keyspace("server"))));
    return builder.build();
}

}

TEST_CASE("load_configuration auto-gates: a flat env stack fails a nested schema loudly", "[auto-gate]")
{
    nucleus::configuration_space space = make_nested_typed_space();

    nucleus::source_stack_options options;
    options.env = nucleus::env_source_options{};

    auto loaded = nucleus::load_configuration(space, options);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("nesting") != std::string::npos);

    // The standalone pre-flight gates the SAME stack and returns the SAME verdict,
    // so a host can validate fit before a load without the two ever disagreeing.
    auto preflight = nucleus::check_capabilities(space, options);
    REQUIRE_FALSE(preflight);
    REQUIRE(preflight.error() == loaded.error());
}

TEST_CASE("a single capable layer satisfies the HARD nesting requirement by union", "[auto-gate]")
{
    nucleus::configuration_space space = make_nested_typed_space();

    capable_source base;
    nucleus::source_stack_options options;
    options.env = nucleus::env_source_options{};
    options.custom_layers.push_back(nucleus::configuration_source_layer{
        &base, static_cast<std::size_t>(nucleus::layer_rank::base), "doc", {}});

    // env lacks nesting, but the custom layer provides it: the whole-stack union
    // satisfies the hard requirement, so the pre-flight gate accepts the stack.
    auto preflight = nucleus::check_capabilities(space, options);
    REQUIRE(preflight);
    bool nesting_honored = false;
    for(nucleus::capability cap : preflight.value().honored)
        if(cap == nucleus::capability::nesting)
            nesting_honored = true;
    REQUIRE(nesting_honored);
}
