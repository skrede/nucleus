// The machine-readable half of the error model: every public failure channel
// must carry the errc that names its pipeline stage, so a host can branch on
// the code instead of parsing the human-readable message.

#include "nucleus/error.h"
#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <cstdint>

using nucleus::errc;
using nucleus::anchor;
using nucleus::source_stack;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

// A source that declares NO capabilities, so a schema requiring nesting cannot
// be satisfied and the gate must abort with unmet_capability.
struct flat_only_source
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const { return {}; }

    [[nodiscard]] nucleus::configuration_source_result pull()
    {
        return nucleus::configuration_source_batch{};
    }
};

struct deny_schema final : nucleus::registration_policy
{
    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        if(request.kind == nucleus::registration_kind::schema)
            return nucleus::policy_verdict::reject("schema surface is closed");
        return nucleus::policy_verdict::accept();
    }
};

}

TEST_CASE("a missing file pulls errc::unreadable_source", "[error][code]")
{
    auto src = nucleus::xml_source::from(
        nucleus::xml_source_options::of_file("definitely-missing-directory/no-such-file.xml"));
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    CHECK(pulled.error().code == errc::unreadable_source);
}

TEST_CASE("garbage xml pulls errc::malformed_source and the load preserves it",
          "[error][code]")
{
    auto pulled = xml_of("<garbage").pull();
    REQUIRE_FALSE(pulled);
    CHECK(pulled.error().code == errc::malformed_source);

    // Through the front door the fold adds the layer label but keeps the code.
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();
    auto loaded = nucleus::load(space, source_stack{xml_of("<garbage")}, {});
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == errc::malformed_source);
}

TEST_CASE("a flat-only stack against a nested schema fails with errc::unmet_capability",
          "[error][code]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("node", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("node"))));
    const nucleus::configuration_space space = builder.build();

    auto loaded = nucleus::load(space, source_stack{flat_only_source{}}, {});
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == errc::unmet_capability);

    auto preflight = nucleus::check_capabilities(space, source_stack{flat_only_source{}}, {});
    REQUIRE_FALSE(preflight);
    CHECK(preflight.error().code == errc::unmet_capability);
}

TEST_CASE("an unknown selection fails with errc::invalid_selection", "[error][code]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node"))));
    const nucleus::configuration_space space = builder.build();

    nucleus::runtime_source src;
    src.set("cluster/node/alpha/name", "alpha")
       .set("cluster/node/alpha/port", "1000");

    nucleus::load_options options;
    options.selection = "nope";
    auto loaded = nucleus::load(space, source_stack{src}, options);
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == errc::invalid_selection);
}

TEST_CASE("an undeclared path fails validation with errc::schema_violation",
          "[error][code]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("server"))));
    const nucleus::configuration_space space = builder.build();

    nucleus::runtime_source src;
    src.set("server/bogus", "x");

    auto loaded = nucleus::load(space, source_stack{src}, {});
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == errc::schema_violation);
}

TEST_CASE("a typed element with a garbage value fails with errc::failed_conversion",
          "[error][code]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::typed_element<std::int32_t>("port", anchor::keyspace("server"))));
    const nucleus::configuration_space space = builder.build();

    nucleus::runtime_source src;
    src.set("server/port", "notanumber");

    auto loaded = nucleus::load(space, source_stack{src}, {});
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == errc::failed_conversion);
}

TEST_CASE("registering on a built builder fails with errc::sealed_builder",
          "[error][code]")
{
    nucleus::configuration_space_builder builder;
    (void)builder.build();

    auto rejected = builder.register_schema("server/host");
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == errc::sealed_builder);
}

TEST_CASE("a rejecting policy fails with errc::rejected_registration and the "
          "verbatim reason", "[error][code]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.set_registration_policy(std::make_shared<deny_schema>()));

    auto rejected = builder.register_element(nucleus::element("server", anchor::root()));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == errc::rejected_registration);
    CHECK(rejected.error().message == "schema surface is closed");
}

TEST_CASE("get_as distinguishes absent_key, missing_converter, and mismatched_type",
          "[error][code]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cfg", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("name", anchor::keyspace("cfg"))));
    REQUIRE(builder.register_element(
        nucleus::typed_element<std::int32_t>("val", anchor::keyspace("cfg"))));
    const nucleus::configuration_space space = builder.build();

    nucleus::runtime_source src;
    src.set("cfg/name", "hello").set("cfg/val", "42");

    auto loaded = nucleus::load(space, source_stack{src}, {});
    REQUIRE(loaded);

    auto absent = loaded.value().get_as<std::int32_t>("cfg/nonexistent");
    REQUIRE_FALSE(absent);
    CHECK(absent.error().code == errc::absent_key);

    auto untyped = loaded.value().get_as<std::int32_t>("cfg/name");
    REQUIRE_FALSE(untyped);
    CHECK(untyped.error().code == errc::missing_converter);

    auto mismatched = loaded.value().get_as<float>("cfg/val");
    REQUIRE_FALSE(mismatched);
    CHECK(mismatched.error().code == errc::mismatched_type);
}
