// The registration-policy seam: surface naming, the accept/reject verdict, the
// permissive default, and a host override that intercepts one surface. Mechanism
// lives in core; policy is the host's -- these cover both ends of that contract.

#include "nucleus/registration_policy.h"
#include "nucleus/identity.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using nucleus::registration_kind;
using nucleus::registration_policy;
using nucleus::registration_request;
using nucleus::policy_verdict;
using nucleus::owner_token;

TEST_CASE("registration_kind names every surface", "[policy]")
{
    CHECK(nucleus::to_string(registration_kind::schema) == "schema");
    CHECK(nucleus::to_string(registration_kind::tokenizer) == "tokenizer");
    CHECK(nucleus::to_string(registration_kind::configuration_source) == "source");
    CHECK(nucleus::to_string(registration_kind::converter) == "converter");
}

TEST_CASE("a policy_verdict carries accept/reject and a reason", "[policy]")
{
    const auto ok = policy_verdict::accept();
    CHECK(ok.accepted());
    CHECK(ok.reason().empty());

    const auto no = policy_verdict::reject("reserved namespace");
    CHECK_FALSE(no.accepted());
    CHECK(no.reason() == "reserved namespace");
}

TEST_CASE("the default registration policy accepts every surface", "[policy]")
{
    registration_policy policy;
    for(const auto kind : {registration_kind::schema, registration_kind::tokenizer,
                           registration_kind::configuration_source,
                           registration_kind::converter})
        CHECK(policy.review(registration_request{kind, owner_token{}}).accepted());
}

namespace {

// A host policy that closes one surface -- proves the seam can intercept before a
// registration commits, with the reason surfaced verbatim.
struct deny_converters final : nucleus::registration_policy
{
    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        if(request.kind == registration_kind::converter)
            return policy_verdict::reject("converters are closed");
        return policy_verdict::accept();
    }
};

}

TEST_CASE("a host policy can reject a specific surface", "[policy]")
{
    deny_converters policy;
    CHECK(policy.review({registration_kind::schema, owner_token{}}).accepted());

    const auto verdict = policy.review({registration_kind::converter, owner_token{}});
    CHECK_FALSE(verdict.accepted());
    CHECK(verdict.reason() == "converters are closed");
}
