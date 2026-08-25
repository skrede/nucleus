// The registration-policy seam: surface naming, the accept/reject verdict, the
// permissive default, and a host override that intercepts one surface. Mechanism
// lives in core; policy is the host's -- these cover both ends of that contract.
// Also the emptiness rejections that sit beside the policy seam: a host callable
// handed over empty is refused where it is handed over, never called where it is used.

#include "support/builder_result_test_support.h"
#include "nucleus/registration_policy.h"
#include "nucleus/identity.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <string>
#include <utility>
#include <typeindex>
#include <functional>
#include <string_view>

using nucleus::registration_kind;
using nucleus::registration_policy;
using nucleus::registration_request;
using nucleus::policy_verdict;
using nucleus::owner_token;

TEST_CASE("registration_kind names every surface", "[policy]")
{
    CHECK(nucleus::to_string(registration_kind::schema) == "schema");
    CHECK(nucleus::to_string(registration_kind::tokenizer) == "tokenizer");
    CHECK(nucleus::to_string(registration_kind::config_source) == "source");
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
                           registration_kind::config_source,
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

TEST_CASE("a tokenizer carrying an empty field resolver is refused at installation",
          "[policy][registration]")
{
    nucleus::config_space_builder builder;
    nucleus::tokenizer tok("host", {nucleus::token_field{"release", nucleus::field_resolver{}}},
                           {}, nullptr);

    const auto installed = builder.install_tokenizer(std::move(tok));
    REQUIRE_FALSE(installed);
    CHECK(installed.error().code == nucleus::errc::rejected_registration);
    CHECK(installed.error().message.find("release") != std::string::npos);
}

TEST_CASE("a tokenizer carrying an empty function resolver is refused at installation",
          "[policy][registration]")
{
    nucleus::config_space_builder builder;
    nucleus::tokenizer tok("host", {},
        {nucleus::token_function{"upper", {}, nucleus::named_function_resolver{}}}, nullptr);

    const auto installed = builder.install_tokenizer(std::move(tok));
    REQUIRE_FALSE(installed);
    CHECK(installed.error().code == nucleus::errc::rejected_registration);
    CHECK(installed.error().message.find("upper") != std::string::npos);
}

TEST_CASE("a tree tokenizer carrying an empty resolver is refused at installation",
          "[policy][registration]")
{
    nucleus::config_space_builder builder;
    const auto installed = builder.install_tree_tokenizer(
        nucleus::tree_tokenizer("hosts", nucleus::tree_field_resolver{}));

    REQUIRE_FALSE(installed);
    CHECK(installed.error().code == nucleus::errc::rejected_registration);
    CHECK(installed.error().message.find("hosts") != std::string::npos);
}

TEST_CASE("an empty converter is refused at registration", "[policy][registration]")
{
    nucleus::config_space_builder builder;
    const auto registered = builder.register_converter<int>(
        std::function<nucleus::expected<std::any, std::string>(std::string_view)>{});

    REQUIRE_FALSE(registered);
    CHECK(registered.error().code == nucleus::errc::rejected_registration);
    CHECK(registered.error().message.find(typeid(int).name()) != std::string::npos);
}

TEST_CASE("a tokenizer that sets no wildcard still installs", "[policy][registration]")
{
    // An empty wildcard is the only spelling of "no wildcard", so the emptiness
    // rejection must not read it as a missing callable.
    nucleus::config_space_builder builder;
    nucleus::tokenizer tok("host",
        {nucleus::token_field{"release", [] { return nucleus::token_result(std::string("1.0")); }}},
        {}, nullptr);

    CHECK(builder.install_tokenizer(std::move(tok)));
    CHECK(builder.install_tokenizer(nucleus::tokenizer("bare", {}, {}, nullptr)));
}

TEST_CASE("document paths named with no parser factory are refused, not skipped",
          "[policy][load]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_schema("k"));
    nucleus::config_space space = nucleus::builder_result_test::built(builder);

    nucleus::load_options options;
    options.document_paths = {"config.xml"};

    const auto loaded = nucleus::load_config(space, nucleus::source_stack{}, options);
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().message.find("config.xml") != std::string::npos);

    // The capability pre-flight consumes the same option and must refuse alike.
    CHECK_FALSE(nucleus::check_capabilities(space, nucleus::source_stack{}, options));
}
