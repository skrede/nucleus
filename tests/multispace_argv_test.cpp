#include "nucleus/log_sink.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config_source/source_stack.h"
#include "nucleus/config_source/source_handle.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/multispace_argv_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

multispace_argv_source::space_view view_of(multispace_argv_source &src, std::string_view name)
{
    auto view = src.for_space(name);
    REQUIRE(view);
    return std::move(view).value();
}

}

TEST_CASE("multispace_argv_source partitions flags by space name",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"--alpha-x=1", "--beta-y=2", "--alpha-z=3"};
    multispace_argv_source src(tokens);
    src.register_space("alpha").register_space("beta");

    auto alpha = view_of(src, "alpha");
    auto beta = view_of(src, "beta");

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

    auto result = view_of(src, "alpha").pull();
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

    auto result = view_of(src, "alpha").pull();
    // A bare space-name flag is silently skipped (it is ambiguous and not addressable).
    REQUIRE(result);
    REQUIRE(result.value().entries.empty());
}

TEST_CASE("multispace_argv_source: bare space flag carrying a value is rejected loudly",
          "[multispace_argv][argv]")
{
    // --alpha=oops attaches a value to the bare space name; a space name is
    // addressing, not an assignable key, so the value has nowhere to bind.
    std::vector<std::string> tokens{"--alpha=oops"};
    multispace_argv_source src(tokens);
    src.register_space("alpha").register_space("beta");

    auto result = view_of(src, "alpha").pull();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::schema_violation);
    REQUIRE(result.error().message.find("alpha") != std::string::npos);
}

TEST_CASE("multispace_argv_source: a space registered after a view is created is addressable",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"--alpha-x=1", "--beta-y=2"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    // View obtained BEFORE beta is registered. The space list is shared live, so
    // beta is addressable to this view without a re-fetch: the beta-addressed
    // flag is skipped, not reported as unaddressed.
    auto alpha = view_of(src, "alpha");
    src.register_space("beta");

    auto result = alpha.pull();
    REQUIRE(result);
    REQUIRE(result.value().entries.size() == 1);
    REQUIRE(result.value().entries[0].path == "x");
}

TEST_CASE("multispace_argv_source: malformed token propagates malformed_source",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"not-a-flag"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    auto result = view_of(src, "alpha").pull();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
}

TEST_CASE("multispace_argv_source: recognizer integration rejects undeclared inner key",
          "[multispace_argv][argv]")
{
    config_space_builder b;
    REQUIRE(b.register_schema("z")); // x is NOT registered
    auto space = nucleus::builder_result_test::built(b);

    std::vector<std::string> tokens{"--alpha-x=1"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    auto alpha = view_of(src, "alpha");
    alpha.recognize_with(recognizer_of(space));

    auto result = alpha.pull();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::schema_violation);
}

TEST_CASE("multispace_argv_source: for_space with an unregistered name reports malformed_source",
          "[multispace_argv][argv]")
{
    multispace_argv_source src({});
    src.register_space("alpha");
    auto view = src.for_space("delta");
    REQUIRE_FALSE(view);
    REQUIRE(view.error().code == errc::malformed_source);
    REQUIRE(view.error().message.find("delta") != std::string::npos);
}

TEST_CASE("multispace_argv_source: lenient policy + log_to passes unknown, emits warning",
          "[multispace_argv][argv]")
{
    std::vector<std::string> tokens{"--alpha-unknown=42"};
    multispace_argv_source src(tokens);
    src.register_space("alpha");

    config_space_builder b;
    REQUIRE(b.register_schema("known"));
    auto space = nucleus::builder_result_test::built(b);

    capturing_sink sink;
    auto alpha = view_of(src, "alpha");
    alpha.recognize_with(recognizer_of(space))
         .policy(unknown_key_policy::lenient)
         .log_to(sink);

    auto result = alpha.pull();
    REQUIRE(result);
    REQUIRE(result.value().entries.size() == 1);
    REQUIRE_FALSE(sink.warnings.empty());
    REQUIRE(sink.warnings[0].find("unknown") != std::string::npos);
}
