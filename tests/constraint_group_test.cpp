#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/constraint_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// CGR-01..05: container-scoped exclusion/choice constraint groups. Domain-neutral
// schema: a `server/cache` policy with mutually-exclusive members, a `server/auth`
// mode selection via all_of bundles, and a Tier-3 validator valve.

using namespace nucleus;

namespace {

// server -> cache{eager, lru, ttl} (exclusion target) + auth{cert, key, token} (choice).
config_space_builder cache_builder()
{
    config_space_builder b;
    REQUIRE(b.register_element(element("server", anchor::root())));
    REQUIRE(b.register_element(element("cache", anchor::keyspace("server"))));
    REQUIRE(b.register_element(element("eager", anchor::keyspace("server/cache"))));
    REQUIRE(b.register_element(element("lru", anchor::keyspace("server/cache"))));
    REQUIRE(b.register_element(element("ttl", anchor::keyspace("server/cache"))));
    return b;
}

bool mentions(const error &e, const char *needle)
{
    return e.message.find(needle) != std::string::npos;
}

}

TEST_CASE("CGR-01: at_most(1) over presence members rejects two active", "[constraint][CGR-01]")
{
    auto b = cache_builder();
    REQUIRE(b.register_constraint_group(
        exclusion_group("cache_policy", anchor::keyspace("server/cache"))
            .members({"eager", "lru", "ttl"}).at_most(1)));
    auto space = std::move(b).build();

    runtime_source one;
    one.set("server/cache/lru", "on");
    REQUIRE(load_config(space, source_stack{std::move(one)}, {}).has_value());

    runtime_source two;
    two.set("server/cache/lru", "on").set("server/cache/ttl", "60");
    auto bad = load_config(space, source_stack{std::move(two)}, {});
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(mentions(bad.error(), "cache_policy"));
    REQUIRE(mentions(bad.error(), "'lru'"));
    REQUIRE(mentions(bad.error(), "'ttl'"));
}

TEST_CASE("CGR-02: when_value activation only counts the matching value", "[constraint][CGR-02]")
{
    auto b = cache_builder();
    REQUIRE(b.register_constraint_group(
        exclusion_group("cache_policy", anchor::keyspace("server/cache"))
            .member("eager", when_value("true"))
            .member("lru")
            .member("ttl")
            .at_most(1)));
    auto space = std::move(b).build();

    // eager=false is inactive; lru present is the single active member -> OK.
    runtime_source ok;
    ok.set("server/cache/eager", "false").set("server/cache/lru", "on");
    REQUIRE(load_config(space, source_stack{std::move(ok)}, {}).has_value());

    // eager=true is active AND lru present -> two active -> violation.
    runtime_source bad;
    bad.set("server/cache/eager", "true").set("server/cache/lru", "on");
    auto r = load_config(space, source_stack{std::move(bad)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "'eager'"));
}

TEST_CASE("CGR-01: exactly(1) and at_least(1) cardinality", "[constraint][CGR-01]")
{
    SECTION("exactly(1): one active satisfies, two active violates")
    {
        auto b = cache_builder();
        REQUIRE(b.register_constraint_group(
            exclusion_group("cache_policy", anchor::keyspace("server/cache"))
                .members({"eager", "lru", "ttl"}).exactly(1)));
        auto space = std::move(b).build();

        runtime_source one;
        one.set("server/cache/lru", "on");
        REQUIRE(load_config(space, source_stack{std::move(one)}, {}).has_value());

        runtime_source two;
        two.set("server/cache/lru", "on").set("server/cache/eager", "x");
        auto r = load_config(space, source_stack{std::move(two)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "exactly"));
    }
    SECTION("at_least(1): an instance with no active member violates")
    {
        auto b = cache_builder();
        REQUIRE(b.register_element(element("note", anchor::keyspace("server/cache"))));
        REQUIRE(b.register_constraint_group(
            exclusion_group("cache_policy", anchor::keyspace("server/cache"))
                .members({"eager", "lru", "ttl"}).at_least(1)));
        auto space = std::move(b).build();
        // Instance materialised by a non-member leaf; no policy member active.
        runtime_source src;
        src.set("server/cache/note", "x");
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "at least"));
    }
}

TEST_CASE("CGR-01: mutually_exclusive sugar desugars to at_most(1)", "[constraint][CGR-01]")
{
    auto b = cache_builder();
    REQUIRE(b.register_constraint_group(
        mutually_exclusive("cache_policy", anchor::keyspace("server/cache"),
                           {"lru", "ttl"})));
    auto space = std::move(b).build();

    runtime_source bad;
    bad.set("server/cache/lru", "on").set("server/cache/ttl", "60");
    auto r = load_config(space, source_stack{std::move(bad)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "cache_policy"));
}

TEST_CASE("CGR-03: choice over all_of bundles selects exactly one", "[constraint][CGR-03]")
{
    config_space_builder b;
    REQUIRE(b.register_element(element("server", anchor::root())));
    REQUIRE(b.register_element(element("auth", anchor::keyspace("server"))));
    REQUIRE(b.register_element(element("cert", anchor::keyspace("server/auth"))));
    REQUIRE(b.register_element(element("key", anchor::keyspace("server/auth"))));
    REQUIRE(b.register_element(element("token", anchor::keyspace("server/auth"))));
    REQUIRE(b.register_constraint_group(
        choice("auth_mode", anchor::keyspace("server/auth"))
            .option(all_of({"cert", "key"}))
            .option(all_of({"token"}))
            .exactly(1)));
    auto space = std::move(b).build();

    SECTION("one complete bundle -> OK")
    {
        runtime_source src;
        src.set("server/auth/cert", "c").set("server/auth/key", "k");
        REQUIRE(load_config(space, source_stack{std::move(src)}, {}).has_value());
    }
    SECTION("both bundles active -> violation (exactly one)")
    {
        runtime_source src;
        src.set("server/auth/cert", "c").set("server/auth/key", "k")
           .set("server/auth/token", "t");
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "auth_mode"));
    }
    SECTION("partial bundle -> all-or-none co-requirement violation")
    {
        runtime_source src;
        src.set("server/auth/cert", "c");  // key missing
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "all-or-none"));
    }
}

TEST_CASE("CGR-04: Tier-3 validate_group valve runs a host predicate", "[constraint][CGR-04]")
{
    auto b = cache_builder();
    REQUIRE(b.register_constraint_group(validate_group(
        "ttl_positive", anchor::keyspace("server/cache"),
        [](const config_node &cache) -> expected<void, std::string> {
            auto ttl = cache["ttl"].value();
            if(ttl.has_value() && *ttl == "0")
                return unexpected(std::string("ttl must not be zero"));
            return {};
        })));
    auto space = std::move(b).build();

    runtime_source ok;
    ok.set("server/cache/ttl", "60");
    REQUIRE(load_config(space, source_stack{std::move(ok)}, {}).has_value());

    runtime_source bad;
    bad.set("server/cache/ttl", "0");
    auto r = load_config(space, source_stack{std::move(bad)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "ttl must not be zero"));
}

TEST_CASE("CGR-05: registration rejects an undefined member loudly", "[constraint][CGR-05]")
{
    auto b = cache_builder();
    auto reg = b.register_constraint_group(
        exclusion_group("cache_policy", anchor::keyspace("server/cache"))
            .members({"lru", "nonsuch"}).at_most(1));
    REQUIRE_FALSE(reg.has_value());
    REQUIRE(reg.error().message.find("nonsuch") != std::string::npos);
}
