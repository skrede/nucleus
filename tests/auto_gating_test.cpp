#include "nucleus/capability.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/config_source/config_source.h"
#include "nucleus/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

// A stub source declaring a capable descriptor (nesting/duplicate_keys/typed),
// used as a custom base layer to prove a single capable layer satisfies a HARD
// requirement that the always-present flat layers cannot (whole-stack union).
// Plain struct satisfying the source concept by duck typing.
struct capable_source
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return nucleus::capability_descriptor{nucleus::capability::nesting,
                                              nucleus::capability::duplicate_keys,
                                              nucleus::capability::typed_scalars};
    }

    [[nodiscard]] nucleus::config_source_result pull()
    {
        return nucleus::config_source_batch{};
    }
};

// A sealed space whose schema is nested (a `server` container primary-keyed by
// `name`) and typed (an int `port` leaf) -- so it derives a HARD nesting
// requirement and a SOFT typed_scalars one. Authoring order is fixed by attach
// referential integrity, but the derived requirement set is order-independent.
[[nodiscard]] nucleus::config_space make_nested_typed_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(
        nucleus::typed_element<int>("port", nucleus::anchor::keyspace("server"))));
    return builder.build();
}

}

TEST_CASE("load auto-gates: a flat env stack fails a nested schema loudly", "[auto-gate]")
{
    nucleus::config_space space = make_nested_typed_space();

    // An empty env source lacks nesting; it cannot satisfy the HARD nesting requirement.
    nucleus::env_source empty_env;
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(empty_env)},
        {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("nesting") != std::string::npos);

    // The standalone pre-flight gates the SAME stack and returns the SAME verdict,
    // so a host can validate fit before a load without the two ever disagreeing.
    nucleus::env_source empty_env2;
    auto preflight = nucleus::check_capabilities(space,
        nucleus::source_stack{std::move(empty_env2)},
        {});
    REQUIRE_FALSE(preflight);
    REQUIRE(preflight.error() == loaded.error());
}

TEST_CASE("a single capable layer satisfies the HARD nesting requirement by union", "[auto-gate]")
{
    nucleus::config_space space = make_nested_typed_space();

    // env lacks nesting, but the capable_source provides it: the whole-stack union
    // satisfies the hard requirement, so the pre-flight gate accepts the stack.
    // env at lower precedence (stack[0]), capable base at higher precedence (stack[1]).
    nucleus::env_source env;
    capable_source base;
    auto preflight = nucleus::check_capabilities(space,
        nucleus::source_stack{std::move(env), std::move(base)},
        {});
    REQUIRE(preflight);
    bool nesting_honored = false;
    for(nucleus::capability cap : preflight.value().honored)
        if(cap == nucleus::capability::nesting)
            nesting_honored = true;
    REQUIRE(nesting_honored);
}
