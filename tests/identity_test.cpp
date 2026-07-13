#include "nucleus/error.h"
#include "nucleus/identity.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_group.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

namespace {

struct noexcept_comparable
{
    bool operator==(const noexcept_comparable &) const noexcept { return true; }
};

struct throwing_comparable
{
    bool operator==(const throwing_comparable &) const { return true; }
};

}

// owner_token::model<T>::equals is noexcept but forwards to T::operator==; a
// throwing comparator would std::terminate inside the noexcept virtual. The ctor
// is constrained to noexcept-comparable payloads, so a throwing comparator is a
// compile-time break rather than a runtime hazard.
static_assert(std::is_constructible_v<nucleus::owner_token, noexcept_comparable>);
static_assert(!std::is_constructible_v<nucleus::owner_token, throwing_comparable>);

TEST_CASE("tokens wrapping equal payloads compare equal", "[identity]")
{
    nucleus::owner_token a(std::string("plugin.a"));
    nucleus::owner_token b(std::string("plugin.a"));
    REQUIRE(a == b);
}

TEST_CASE("tokens wrapping different payloads compare unequal", "[identity]")
{
    nucleus::owner_token a(std::string("plugin.a"));
    nucleus::owner_token b(std::string("plugin.b"));
    REQUIRE(a != b);
}

TEST_CASE("tokens of different payload types are never equal", "[identity]")
{
    nucleus::owner_token text(std::string("7"));
    nucleus::owner_token number(7);
    REQUIRE(text != number);
}

TEST_CASE("anonymous tokens are distinct identities", "[identity]")
{
    nucleus::owner_token a;
    nucleus::owner_token b;
    REQUIRE_FALSE(a.has_value());
    REQUIRE(a != b);
    REQUIRE(a == a);
}

TEST_CASE("the core treats structurally-different token types uniformly", "[identity]")
{
    // The token surface exposes only construction and equality -- there is no
    // accessor to the payload, so no core code path can branch on its value or
    // type. Heterogeneous tokens coexist and are merely compared.
    nucleus::owner_token by_string(std::string("owner"));
    nucleus::owner_token by_int(123);
    REQUIRE(by_string != by_int);
    REQUIRE(by_string == by_string);
    REQUIRE(by_int == by_int);
}

// A malformed anchor path (from anchor::keyspace(string)) must fail loudly at
// registration with errc::malformed_source instead of silently re-anchoring at root.
TEST_CASE("a malformed anchor path is rejected loudly at registration", "[identity][anchor]")
{
    using namespace nucleus;

    SECTION("register_element rejects a malformed anchor with errc::malformed_source")
    {
        config_space_builder b;
        auto rejected = b.register_element(element("port", anchor::keyspace("a//b")));
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error().code == errc::malformed_source);
    }

    SECTION("register_constraint_group rejects a malformed anchor")
    {
        config_space_builder b;
        auto rejected = b.register_constraint_group(
            exclusion_group("cache_policy", anchor::keyspace("a//b"))
                .members({"eager"}).at_most(1));
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error().code == errc::malformed_source);
    }

    SECTION("register_identity_group rejects a malformed anchor")
    {
        config_space_builder b;
        auto rejected = b.register_identity_group(
            identity_group("component_names", anchor::keyspace("a//b"))
                .members({"worker"}).field("name"));
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error().code == errc::malformed_source);
    }

    SECTION("a well-formed anchor still registers -- no over-rejection")
    {
        config_space_builder b;
        REQUIRE(b.register_element(element("server", anchor::root())));
        REQUIRE(b.register_element(element("port", anchor::keyspace("server"))));
    }
}
