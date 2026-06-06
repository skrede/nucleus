#include "nucleus/identity.h"

#include "nucleus/host/host_tokenizer.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using nucleus::owner_token;
using nucleus::resolve_tokens;
using nucleus::tokenizer_registry;

TEST_CASE("the HOST module is off by default: an unregistered HOST token is unknown",
          "[host][optin]")
{
    // A bare core registry has no HOST tokenizer -- the module must be opted in.
    tokenizer_registry reg;
    auto r = resolve_tokens("${HOST.hostname}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == nucleus::resolve_errc::unknown_category);
}

TEST_CASE("once opted in, HOST fields resolve", "[host][optin]")
{
    tokenizer_registry reg;
    reg.add(nucleus::make_host_tokenizer(), owner_token{});

    auto hostname = resolve_tokens("${HOST.hostname}", reg);
    REQUIRE(hostname.has_value());
    CHECK_FALSE(hostname.value().empty());

    auto user = resolve_tokens("${HOST.username}", reg);
    REQUIRE(user.has_value());
    CHECK_FALSE(user.value().empty());

    // An unknown HOST field is a named miss, not an empty expansion.
    auto bad = resolve_tokens("${HOST.nonsense}", reg);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == nucleus::resolve_errc::missing_field);
}
