#include "nucleus/configuration_space.h"
#include "nucleus/identity.h"
#include "nucleus/log_sink.h"
#include "nucleus/capability.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/configuration_source/feature_gate.h"

#include "nucleus/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace {

// A host policy that records every reviewed registration and rejects tokenizers
// whose review it is configured to refuse -- proving the seam can intercept.
class recording_policy final : public nucleus::registration_policy
{
public:
    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        seen.push_back(request.kind);
        if(request.kind == nucleus::registration_kind::tokenizer && reject_tokenizers)
            return nucleus::policy_verdict::reject("tokenizers are reserved by the host");
        return nucleus::policy_verdict::accept();
    }

    std::vector<nucleus::registration_kind> seen;
    bool reject_tokenizers = false;
};

}

TEST_CASE("the facade accepts registrations on all surfaces", "[facade]")
{
    nucleus::configuration_space_builder engine;

    // The generic core tokenizers (env/string) are installed by default, so
    // a fresh facade already carries them; a host registration adds on top.
    const std::size_t builtin_tokenizers = engine.tokenizer_count();
    REQUIRE(builtin_tokenizers >= 2);

    REQUIRE(engine.register_schema("logging/level"));
    REQUIRE(engine.register_tokenizer("custom"));

    REQUIRE(engine.schema_count() == 1);
    REQUIRE(engine.tokenizer_count() == builtin_tokenizers + 1);
}

TEST_CASE("each registration carries an opaque owner token", "[facade]")
{
    nucleus::configuration_space_builder engine;
    nucleus::owner_token host_a(std::string("plugin.a"));
    nucleus::owner_token host_b(42);

    REQUIRE(engine.register_schema("a/b", host_a));
    REQUIRE(engine.register_tokenizer("env-extra", host_b));
    REQUIRE(engine.schema_count() == 1);
    REQUIRE(engine.tokenizer_count() >= 3); // 2 builtins + 1 registered
}

TEST_CASE("the registration-policy seam can intercept a registration", "[facade]")
{
    auto policy = std::make_shared<recording_policy>();
    policy->reject_tokenizers = true;

    nucleus::configuration_space_builder engine;
    REQUIRE(engine.set_registration_policy(policy));

    REQUIRE(engine.register_schema("a"));
    auto rejected = engine.register_tokenizer("custom");
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == "tokenizers are reserved by the host");

    // The schema registration committed; the tokenizer registration did not.
    REQUIRE(engine.schema_count() == 1);

    // The policy saw both reviews before either committed.
    REQUIRE(policy->seen.size() == 2);
    REQUIRE(policy->seen[0] == nucleus::registration_kind::schema);
    REQUIRE(policy->seen[1] == nucleus::registration_kind::tokenizer);
}

TEST_CASE("two registrations claiming the same key path surface a non-adjudicating conflict",
          "[facade][conflict]")
{
    nucleus::configuration_space_builder engine;
    nucleus::owner_token plugin_a(std::string("plugin.a"));
    nucleus::owner_token plugin_b(std::string("plugin.b"));

    // No conflict until a second claim of the same path.
    REQUIRE(engine.register_schema("server/port", plugin_a));
    REQUIRE(engine.conflicts().empty());

    // A second registration claims the same key path -- a duplicate claim.
    REQUIRE(engine.register_schema("server/port", plugin_b));

    auto reports = engine.conflicts();
    REQUIRE(reports.size() == 1);
    const nucleus::conflict_report &report = reports.front();
    REQUIRE(report.key_path() == "server/port");
    REQUIRE(report.size() == 2);

    // The report names the colliding key and refuses to pick a winner; both
    // claimants' owner tokens travel for host adjudication.
    const std::string text = report.describe();
    REQUIRE(text.find("server/port") != std::string::npos);
    REQUIRE(text.find("no winner") != std::string::npos);
    REQUIRE(report.claimants().front().owner == plugin_a);
    REQUIRE(report.claimants().back().owner == plugin_b);

    // The duplicate registration still committed -- the core surfaces, it does not
    // reject (that would be adjudication).
    REQUIRE(engine.schema_count() == 2);
}

TEST_CASE("a typed element double-claiming a path conflicts with a path registration",
          "[facade][conflict]")
{
    nucleus::configuration_space_builder engine;
    auto logging = nucleus::key_path::parse("logging");
    REQUIRE(logging);

    REQUIRE(engine.register_schema("logging/level"));
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("level", nucleus::anchor::keyspace(logging.value()))));

    // logging/level was claimed both by the path registration and the element.
    auto reports = engine.conflicts();
    REQUIRE(reports.size() == 1);
    REQUIRE(reports.front().key_path() == "logging/level");
    REQUIRE(reports.front().size() == 2);
}

TEST_CASE("the per-source capability gate applies the loud/quiet contract", "[facade][capability]")
{
    // gate_features is the per-source primitive a host calls directly to gate a
    // single source's capabilities. A required capability env lacks fails loudly;
    // an optional one degrades observably.
    struct counting_sink final : nucleus::log_sink
    {
        void log(nucleus::log_level, std::string_view) override { ++count; }
        int count = 0;
    } sink;

    nucleus::env_source env;

    std::vector<nucleus::feature_requirement> required{
        {nucleus::capability::nesting, nucleus::requirement_strength::required}};
    auto refused = nucleus::gate_features("schema", "env", env.capabilities(), required, sink);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().find("nesting") != std::string::npos);
    REQUIRE(refused.error().find("env") != std::string::npos);

    std::vector<nucleus::feature_requirement> optional{
        {nucleus::capability::ordering, nucleus::requirement_strength::optional}};
    auto degraded = nucleus::gate_features("schema", "env", env.capabilities(), optional, sink);
    REQUIRE(degraded);
    REQUIRE(degraded.value().degraded.size() == 1);
    REQUIRE(sink.count == 1); // the degradation was surfaced through the sink.
}

TEST_CASE("clearing the policy restores accept-all behavior", "[facade]")
{
    auto policy = std::make_shared<recording_policy>();
    policy->reject_tokenizers = true;

    nucleus::configuration_space_builder engine;
    REQUIRE(engine.set_registration_policy(policy));
    const std::size_t builtin_count = engine.tokenizer_count();
    REQUIRE_FALSE(engine.register_tokenizer("custom"));

    REQUIRE(engine.set_registration_policy(nullptr));
    REQUIRE(engine.register_tokenizer("custom"));
    REQUIRE(engine.tokenizer_count() == builtin_count + 1);
}
