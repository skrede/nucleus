#include "nucleus/nucleus.h"
#include "nucleus/identity.h"
#include "nucleus/registration_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace {

// A host policy that records every reviewed registration and rejects sources
// whose review it is configured to refuse -- proving the seam can intercept.
class recording_policy final : public nucleus::registration_policy
{
public:
    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        seen.push_back(request.kind);
        if(request.kind == nucleus::registration_kind::source && reject_sources)
            return nucleus::policy_verdict::reject("sources are reserved by the host");
        return nucleus::policy_verdict::accept();
    }

    std::vector<nucleus::registration_kind> seen;
    bool reject_sources = false;
};

}

TEST_CASE("the facade accepts registrations on all three surfaces", "[facade]")
{
    nucleus::nucleus engine;

    // The generic core tokenizers (env/uuid/string) are installed by default, so
    // a fresh facade already carries them; a host registration adds on top.
    const std::size_t builtin_tokenizers = engine.tokenizer_count();
    REQUIRE(builtin_tokenizers >= 3);

    REQUIRE(engine.register_schema("logging/level"));
    REQUIRE(engine.register_tokenizer("custom"));
    REQUIRE(engine.register_source("argv"));

    REQUIRE(engine.schema_count() == 1);
    REQUIRE(engine.tokenizer_count() == builtin_tokenizers + 1);
    REQUIRE(engine.source_count() == 1);
}

TEST_CASE("each registration carries an opaque owner token", "[facade]")
{
    nucleus::nucleus engine;
    nucleus::owner_token host_a(std::string("plugin.a"));
    nucleus::owner_token host_b(42);

    REQUIRE(engine.register_schema("a/b", host_a));
    REQUIRE(engine.register_source("env", host_b));
    REQUIRE(engine.schema_count() == 1);
    REQUIRE(engine.source_count() == 1);
}

TEST_CASE("the registration-policy seam can intercept a registration", "[facade]")
{
    auto policy = std::make_shared<recording_policy>();
    policy->reject_sources = true;

    nucleus::nucleus engine;
    engine.set_registration_policy(policy);

    REQUIRE(engine.register_schema("a"));
    auto rejected = engine.register_source("argv");
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == "sources are reserved by the host");

    // The schema registration committed; the source registration did not.
    REQUIRE(engine.schema_count() == 1);
    REQUIRE(engine.source_count() == 0);

    // The policy saw both reviews before either committed.
    REQUIRE(policy->seen.size() == 2);
    REQUIRE(policy->seen[0] == nucleus::registration_kind::schema);
    REQUIRE(policy->seen[1] == nucleus::registration_kind::source);
}

TEST_CASE("clearing the policy restores accept-all behavior", "[facade]")
{
    auto policy = std::make_shared<recording_policy>();
    policy->reject_sources = true;

    nucleus::nucleus engine;
    engine.set_registration_policy(policy);
    REQUIRE_FALSE(engine.register_source("argv"));

    engine.set_registration_policy(nullptr);
    REQUIRE(engine.register_source("argv"));
    REQUIRE(engine.source_count() == 1);
}
