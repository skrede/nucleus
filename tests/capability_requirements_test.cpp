#include "nucleus/log_sink.h"
#include "nucleus/capability.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/capability_requirements.h"

#include "nucleus/configuration_source/feature_gate.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>

namespace {

// A log_sink that records every message so a test can prove a soft degradation
// was surfaced rather than silently dropped.
class recording_sink final : public nucleus::log_sink
{
public:
    void log(nucleus::log_level level, std::string_view message) override
    {
        messages.emplace_back(level, std::string(message));
    }

    std::vector<std::pair<nucleus::log_level, std::string>> messages;
};

}

TEST_CASE("a flat, untyped, non-repeated schema derives no requirements", "[capability]")
{
    nucleus::schema_registry schema;
    REQUIRE(schema.attach(nucleus::element("name", nucleus::anchor::root())));

    auto reqs = nucleus::derive_capability_requirements(schema.elements());
    REQUIRE(reqs.empty());
}

TEST_CASE("a nested schema derives exactly one HARD nesting requirement", "[capability]")
{
    nucleus::schema_registry schema;
    REQUIRE(schema.attach(nucleus::primary_key_element("name", nucleus::anchor::root())));
    // Two nested leaves under the same container: derivation must dedupe to one.
    REQUIRE(schema.attach(nucleus::element("host", nucleus::anchor::keyspace("name"))));
    REQUIRE(schema.attach(nucleus::element("port", nucleus::anchor::keyspace("name"))));

    auto reqs = nucleus::derive_capability_requirements(schema.elements());
    REQUIRE(reqs.size() == 1);
    REQUIRE(reqs[0].cap == nucleus::capability::nesting);
    REQUIRE(reqs[0].strength == nucleus::requirement_strength::required);
}

TEST_CASE("a repeated element derives a HARD duplicate_keys requirement", "[capability]")
{
    nucleus::schema_registry schema;
    REQUIRE(schema.attach(nucleus::repeated_element("tag", nucleus::anchor::root())));

    auto reqs = nucleus::derive_capability_requirements(schema.elements());
    REQUIRE(reqs.size() == 1);
    REQUIRE(reqs[0].cap == nucleus::capability::duplicate_keys);
    REQUIRE(reqs[0].strength == nucleus::requirement_strength::required);
}

TEST_CASE("a typed element derives a SOFT typed_scalars requirement", "[capability]")
{
    nucleus::schema_registry schema;
    REQUIRE(schema.attach(nucleus::typed_element<int>("port", nucleus::anchor::root())));

    auto reqs = nucleus::derive_capability_requirements(schema.elements());
    REQUIRE(reqs.size() == 1);
    REQUIRE(reqs[0].cap == nucleus::capability::typed_scalars);
    REQUIRE(reqs[0].strength == nucleus::requirement_strength::optional);
}

TEST_CASE("gate_stack honors a HARD capability when any layer in the stack provides it", "[capability]")
{
    recording_sink log;
    std::vector<std::pair<std::string, nucleus::capability_descriptor>> layers{
        {"xml", nucleus::capability_descriptor{nucleus::capability::nesting,
                                               nucleus::capability::duplicate_keys}},
        {"env", nucleus::capability_descriptor{}},
    };
    std::vector<nucleus::feature_requirement> reqs{
        {nucleus::capability::nesting, nucleus::requirement_strength::required}};

    auto gated = nucleus::gate_stack("schema", layers, reqs, log);
    REQUIRE(gated);
    REQUIRE(gated.value().honored.size() == 1);
    REQUIRE(gated.value().honored[0] == nucleus::capability::nesting);
    REQUIRE(log.messages.empty());
}

TEST_CASE("gate_stack errors loudly naming the capability and every layer when none provide a HARD cap", "[capability]")
{
    recording_sink log;
    std::vector<std::pair<std::string, nucleus::capability_descriptor>> layers{
        {"env", nucleus::capability_descriptor{}},
        {"argv", nucleus::capability_descriptor{}},
    };
    std::vector<nucleus::feature_requirement> reqs{
        {nucleus::capability::nesting, nucleus::requirement_strength::required}};

    auto gated = nucleus::gate_stack("schema", layers, reqs, log);
    REQUIRE_FALSE(gated);
    const std::string &message = gated.error().message;
    REQUIRE(message.find("nesting") != std::string::npos);
    REQUIRE(message.find("env") != std::string::npos);
    REQUIRE(message.find("argv") != std::string::npos);
}

TEST_CASE("gate_stack degrades a SOFT capability no layer provides, and the load proceeds", "[capability]")
{
    recording_sink log;
    std::vector<std::pair<std::string, nucleus::capability_descriptor>> layers{
        {"env", nucleus::capability_descriptor{}},
    };
    std::vector<nucleus::feature_requirement> reqs{
        {nucleus::capability::typed_scalars, nucleus::requirement_strength::optional}};

    auto gated = nucleus::gate_stack("schema", layers, reqs, log);
    REQUIRE(gated);
    REQUIRE(gated.value().honored.empty());
    REQUIRE(gated.value().degraded.size() == 1);
    REQUIRE(gated.value().degraded[0].cap == nucleus::capability::typed_scalars);
    REQUIRE(log.messages.size() == 1);
    REQUIRE(log.messages[0].first == nucleus::log_level::warn);
    REQUIRE(log.messages[0].second.find("typed_scalars") != std::string::npos);
}
