#include "nucleus/config.h"
#include "nucleus/log_sink.h"
#include "nucleus/capability.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/config_source/config_source.h"
#include "nucleus/config_source/feature_gate.h"

#include "nucleus/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace {

// A log_sink that records every message it receives, so a test can assert that
// optional-feature degradation was actually surfaced (not silently dropped).
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

TEST_CASE("env declares an honestly-restrictive capability descriptor", "[capability]")
{
    nucleus::env_source env;
    auto caps = env.capabilities();
    REQUIRE_FALSE(caps.supports(nucleus::capability::nesting));
    REQUIRE_FALSE(caps.supports(nucleus::capability::duplicate_keys));
    REQUIRE_FALSE(caps.supports(nucleus::capability::typed_scalars));
    REQUIRE_FALSE(caps.supports(nucleus::capability::comments));
    REQUIRE_FALSE(caps.supports(nucleus::capability::ordering));
}

TEST_CASE("a required-but-unsatisfiable capability is a loud error naming both parties", "[capability]")
{
    recording_sink log;
    nucleus::env_source env;

    std::vector<nucleus::feature_requirement> reqs{
        {nucleus::capability::nesting, nucleus::requirement_strength::required},
    };

    auto gated = nucleus::gate_features("logging-schema", "env", env.capabilities(), reqs, log);

    REQUIRE_FALSE(gated);                       // gating fails loudly.
    const std::string &message = gated.error().message;
    // The diagnostic names BOTH parties and the capability.
    REQUIRE(message.find("env") != std::string::npos);
    REQUIRE(message.find("logging-schema") != std::string::npos);
    REQUIRE(message.find("nesting") != std::string::npos);
}

TEST_CASE("an optional-but-absent capability degrades observably, not silently", "[capability]")
{
    recording_sink log;
    nucleus::env_source env;

    std::vector<nucleus::feature_requirement> reqs{
        {nucleus::capability::ordering, nucleus::requirement_strength::optional},
    };

    auto gated = nucleus::gate_features("ordered-list", "env", env.capabilities(), reqs, log);

    REQUIRE(gated);                              // resolution proceeds.
    REQUIRE(gated.value().honored.empty());
    REQUIRE(gated.value().degraded.size() == 1);
    REQUIRE(gated.value().degraded[0].cap == nucleus::capability::ordering);

    // The degradation was surfaced through the log_sink at warn level.
    REQUIRE(log.messages.size() == 1);
    REQUIRE(log.messages[0].first == nucleus::log_level::warn);
    REQUIRE(log.messages[0].second.find("ordering") != std::string::npos);

    // And recorded as a provenance note carrying both parties.
    const std::string &note = gated.value().degraded[0].note;
    REQUIRE(note.find("env") != std::string::npos);
    REQUIRE(note.find("ordered-list") != std::string::npos);
}

TEST_CASE("the descriptor changes behavior: the same requirement passes for a capable source", "[capability]")
{
    recording_sink log;

    // A source whose descriptor DOES support nesting -- same requirement, the
    // opposite outcome. This is the proof that gating is data-driven, not a stub:
    // behavior tracks the descriptor.
    nucleus::capability_descriptor capable{nucleus::capability::nesting};

    std::vector<nucleus::feature_requirement> reqs{
        {nucleus::capability::nesting, nucleus::requirement_strength::required},
    };

    auto env_gate = nucleus::gate_features("s", "env", nucleus::env_source{}.capabilities(), reqs, log);
    auto cap_gate = nucleus::gate_features("s", "doc", capable, reqs, log);

    REQUIRE_FALSE(env_gate);                     // env lacks nesting -> error.
    REQUIRE(cap_gate);                           // capable source -> honored.
    REQUIRE(cap_gate.value().honored.size() == 1);
    REQUIRE(cap_gate.value().honored[0] == nucleus::capability::nesting);
}

TEST_CASE("a load-time degradation reaches both the host sink and config::degradations()",
          "[capability]")
{
    // A typed root element implies a SOFT typed_scalars requirement; env declares
    // no such capability, so loading a typed field from env degrades observably.
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
        nucleus::typed_element<std::int32_t>("port", nucleus::anchor::root())));
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);

    recording_sink log;
    nucleus::env_source env;
    env.set("port", "7000");

    nucleus::load_options options;
    options.log = &log;

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(env)}, options);
    REQUIRE(loaded);

    // Channel 1: the host sink received the warn-level degradation notice.
    REQUIRE(log.messages.size() == 1);
    REQUIRE(log.messages[0].first == nucleus::log_level::warn);
    REQUIRE(log.messages[0].second.find("typed_scalars") != std::string::npos);

    // Channel 2: the frozen config carries the same degradation as provenance.
    const std::span<const nucleus::degradation> degraded = loaded.value().degradations();
    REQUIRE(degraded.size() == 1);
    REQUIRE(degraded[0].cap == nucleus::capability::typed_scalars);

    // The two channels are the SAME list: the recorded note equals the logged text.
    REQUIRE(degraded[0].note == log.messages[0].second);
}

TEST_CASE("the provenance channel records the degradation even without a host sink",
          "[capability]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
        nucleus::typed_element<std::int32_t>("port", nucleus::anchor::root())));
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);

    nucleus::env_source env;
    env.set("port", "7000");

    // No options.log supplied (nullptr): provenance does not depend on a host sink.
    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(env)}, {});
    REQUIRE(loaded);

    const std::span<const nucleus::degradation> degraded = loaded.value().degradations();
    REQUIRE(degraded.size() == 1);
    REQUIRE(degraded[0].cap == nucleus::capability::typed_scalars);
}

TEST_CASE("env emits keyspace entries directly with no document model", "[capability]")
{
    nucleus::env_source env;
    env.set("logging/level", "debug").set("logging/file", "/var/log/app");

    auto pulled = env.pull();
    REQUIRE(pulled);
    auto &batch = pulled.value();

    REQUIRE(batch.entries.size() == 2);
    REQUIRE(batch.entries[0].path == "logging/level");
    REQUIRE(batch.entries[0].value.text() == "debug");
    REQUIRE(batch.entries[0].value.is_owned());
    // A non-document source pins no buffer: its values are self-owned.
    REQUIRE_FALSE(batch.buffer.pins_anything());
}
