#include "nucleus/log_sink.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/configuration_source/source_stack.h"
#include "nucleus/configuration_source/source_handle.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/multispace_argv_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <stdexcept>

using namespace nucleus;

namespace {

class capturing_sink final : public log_sink
{
public:
    void log(log_level level, std::string_view message) override
    {
        if(level == log_level::warn)
            warnings.emplace_back(message);
    }

    std::vector<std::string> warnings;
};

}

TEST_CASE("multispace_argv_source partitions flags by space name",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"--alpha-x=1", "--beta-y=2", "--alpha-z=3"};
    multispace_argv_source src(tokens);
    src.register_space("alpha").register_space("beta");

    auto alpha = src.for_space("alpha");
    auto beta = src.for_space("beta");

    auto ar = alpha.pull();
    REQUIRE(ar);
    REQUIRE(ar.value().entries.size() == 2);

    auto br = beta.pull();
    REQUIRE(br);
    REQUIRE(br.value().entries.size() == 1);

    // Alpha gets x and z; beta gets y.
    std::vector<std::string> alpha_keys;
    for(const auto &e : ar.value().entries)
        alpha_keys.push_back(e.path);
    REQUIRE(std::find(alpha_keys.begin(), alpha_keys.end(), "x") != alpha_keys.end());
    REQUIRE(std::find(alpha_keys.begin(), alpha_keys.end(), "z") != alpha_keys.end());

    REQUIRE(br.value().entries[0].path == "y");
    REQUIRE(br.value().entries[0].value.text() == "2");
}

TEST_CASE("multispace_argv_source: unaddressed flag yields schema_violation naming spaces",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"--gamma-w=1"};
    multispace_argv_source src(tokens);
    src.register_space("alpha").register_space("beta");

    auto alpha = src.for_space("alpha");
    auto result = alpha.pull();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::schema_violation);
    REQUIRE(result.error().message.find("gamma") != std::string::npos);
    REQUIRE(result.error().message.find("alpha") != std::string::npos);
    REQUIRE(result.error().message.find("beta") != std::string::npos);
}

TEST_CASE("multispace_argv_source: bare space name flag is skipped (not an addressed flag)",
          "[multispace_argv][argv]")
{
    // --alpha (bare, no sub-segment) has only one segment after parsing.
    // It belongs to the alpha space but has no inner key, so it should be skipped.
    std::vector<std::string> tokens{"--alpha"};
    multispace_argv_source src(tokens);
    src.register_space("alpha").register_space("beta");

    auto alpha = src.for_space("alpha");
    auto result = alpha.pull();
    // A bare space-name flag is silently skipped (it is ambiguous and not addressable).
    REQUIRE(result);
    REQUIRE(result.value().entries.empty());
}

TEST_CASE("multispace_argv_source: malformed token propagates malformed_source",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"not-a-flag"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    auto alpha = src.for_space("alpha");
    auto result = alpha.pull();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
}

TEST_CASE("multispace_argv_source: recognizer integration rejects undeclared inner key",
          "[multispace_argv][argv]")
{
    configuration_space_builder b;
    REQUIRE(b.register_schema("z")); // x is NOT registered
    auto space = b.build();

    std::vector<std::string> tokens{"--alpha-x=1"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    auto alpha = src.for_space("alpha");
    alpha.recognize_with(recognizer_of(space));

    auto result = alpha.pull();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::schema_violation);
}

TEST_CASE("multispace_argv_source: for_space with unregistered name throws invalid_argument",
          "[multispace_argv][argv]")
{
    multispace_argv_source src({});
    src.register_space("alpha");
    REQUIRE_THROWS_AS(src.for_space("delta"), std::invalid_argument);
}

TEST_CASE("multispace_argv_source: lenient policy + log_to passes unknown, emits warning",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"--alpha-unknown=42"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    configuration_space_builder b;
    REQUIRE(b.register_schema("known"));
    auto space = b.build();

    capturing_sink sink;
    auto alpha = src.for_space("alpha");
    alpha.recognize_with(recognizer_of(space))
         .policy(unknown_key_policy::lenient)
         .log_to(sink);

    auto result = alpha.pull();
    REQUIRE(result);
    REQUIRE(result.value().entries.size() == 1);
    REQUIRE_FALSE(sink.warnings.empty());
    REQUIRE(sink.warnings[0].find("unknown") != std::string::npos);
}
