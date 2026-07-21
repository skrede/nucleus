#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/constraint_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// container-scoped exclusion/choice constraint groups. Domain-neutral
// schema: a `server/cache` policy with mutually-exclusive members, a `server/auth`
// mode selection via all_of bundles, and a host-validator valve.

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

TEST_CASE("at_most(1) over presence members rejects two active", "[constraint]")
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

TEST_CASE("when_value activation only counts the matching value", "[constraint]")
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

TEST_CASE("when_value activation finds a member at an indexed instance path", "[constraint]")
{
    auto make = [] {
        config_space_builder b;
        REQUIRE(b.register_element(element("server", anchor::root())));
        REQUIRE(b.register_element(element("cache", anchor::keyspace("server"))));
        REQUIRE(b.register_element(
            repeated_element("eager", anchor::keyspace("server/cache"))));
        REQUIRE(b.register_element(element("lru", anchor::keyspace("server/cache"))));
        REQUIRE(b.register_constraint_group(
            exclusion_group("cache_policy", anchor::keyspace("server/cache"))
                .member("eager", when_value("true"))
                .member("lru")
                .at_most(1)));
        return std::move(b).build();
    };

    // A single repeated value is stored at the indexed path server/cache/eager[0];
    // the plain path carries no scalar.
    SECTION("indexed eager=true plus lru -> two active -> violation")
    {
        auto space = make();
        runtime_source src;
        src.set("server/cache/eager", "true").set("server/cache/lru", "on");
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "cache_policy"));
        REQUIRE(mentions(r.error(), "'eager'"));
    }
    SECTION("exact-match: a case-differing indexed value does not activate")
    {
        auto space = make();
        runtime_source src;
        src.set("server/cache/eager", "TRUE").set("server/cache/lru", "on");
        REQUIRE(load_config(space, source_stack{std::move(src)}, {}).has_value());
    }
}

TEST_CASE("when_value activation fires per-instance under a repeated container", "[constraint]")
{
    config_space_builder b;
    REQUIRE(b.register_element(repeated_element("pool", anchor::root())));
    REQUIRE(b.register_element(element("mode", anchor::keyspace("pool"))));
    REQUIRE(b.register_element(element("lru", anchor::keyspace("pool"))));
    REQUIRE(b.register_constraint_group(
        exclusion_group("pool_policy", anchor::keyspace("pool"))
            .member("mode", when_value("active"))
            .member("lru")
            .at_most(1)));
    auto space = std::move(b).build();

    // pool[0]: mode=active + lru=on -> two active -> violation on this instance.
    // pool[1]: mode=idle   + lru=on -> mode inactive -> one active -> fine.
    runtime_source src;
    src.set("pool[0]/mode", "active").set("pool[0]/lru", "on")
       .set("pool[1]/mode", "idle").set("pool[1]/lru", "on");
    auto r = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "pool_policy"));
    REQUIRE(mentions(r.error(), "'mode'"));
    REQUIRE(mentions(r.error(), "pool[0]"));
}

TEST_CASE("exactly(1) and at_least(1) cardinality", "[constraint]")
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

TEST_CASE("mutually_exclusive sugar desugars to at_most(1)", "[constraint]")
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

TEST_CASE("Choice over all_of bundles selects exactly one", "[constraint]")
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

TEST_CASE("validate_group valve runs a host predicate", "[constraint]")
{
    auto b = cache_builder();
    REQUIRE(b.register_constraint_group(validate_group(
        "ttl_positive", anchor::keyspace("server/cache"),
        [](const config_node &cache) -> expected<void, std::string> {
            auto ttl = cache["ttl"].value();
            if(ttl.has_value() && *ttl == "0")
                return nucleus::unexpected(std::string("ttl must not be zero"));
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

TEST_CASE("Root-anchored group-only schema enforces on an empty surface", "[constraint]")
{
    auto make = [](bool reject) {
        config_space_builder b;
        REQUIRE(b.register_constraint_group(validate_group(
            "root_valve", anchor::root(),
            [reject](const config_node &) -> expected<void, std::string> {
                if(reject)
                    return nucleus::unexpected(std::string("host rejected the root"));
                return {};
            })));
        return std::move(b).build();
    };

    SECTION("rejecting validator fails the load")
    {
        auto space = make(true);
        runtime_source src;
        src.set("anything", "x");
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "host rejected the root"));
    }
    SECTION("passing validator loads clean")
    {
        auto space = make(false);
        runtime_source src;
        src.set("anything", "x");
        REQUIRE(load_config(space, source_stack{std::move(src)}, {}).has_value());
    }
}

TEST_CASE("Registration rejects an undefined member loudly", "[constraint]")
{
    auto b = cache_builder();
    auto reg = b.register_constraint_group(
        exclusion_group("cache_policy", anchor::keyspace("server/cache"))
            .members({"lru", "nonsuch"}).at_most(1));
    REQUIRE_FALSE(reg.has_value());
    REQUIRE(reg.error().message.find("nonsuch") != std::string::npos);
}
