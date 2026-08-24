#include "nucleus/config.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <map>
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace {

using raw_values   = std::map<std::string, std::string>;
using typed_values = std::map<std::string, std::any>;

constexpr std::array<std::size_t, 4> ordinals{0, 1, 2, 10};

static_assert(std::is_default_constructible_v<nucleus::config>);
static_assert(!std::is_constructible_v<nucleus::config,
                                       raw_values, nucleus::provenance>);
static_assert(!std::is_constructible_v<nucleus::config,
                                       raw_values, typed_values, nucleus::provenance,
                                       std::vector<nucleus::degradation>>);

bool mentions(const nucleus::error &failure, const std::string &text)
{
    return failure.message.find(text) != std::string::npos;
}

void require_malformed_path(const std::string &path)
{
    auto made = nucleus::config::from_values({{path, "value"}});
    REQUIRE_FALSE(made);
    CHECK(made.error().code == nucleus::errc::malformed_source);
    CHECK(mentions(made.error(), "'" + path + "'"));
}

nucleus::runtime_source mixed_source()
{
    nucleus::runtime_source source;
    source.set("tags", "plain").set("tags[0]", "zero");
    return source;
}

void require_mixed_rejection(const nucleus::config_space &space)
{
    auto loaded = nucleus::load_config(
            space, nucleus::source_stack{mixed_source()}, {});
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::schema_violation);
    CHECK(mentions(loaded.error(), "canonical path 'tags'"));
    CHECK(mentions(loaded.error(), "concrete paths 'tags' and 'tags[0]'"));
}

nucleus::config_space empty_space()
{
    nucleus::config_space_builder builder;
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space untyped_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("tags", nucleus::anchor::root())));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space typed_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::typed_element<std::int32_t>(
            "tags", nucleus::anchor::root())));
    return nucleus::builder_result_test::built(builder);
}

}

TEST_CASE("checked config construction accepts one storage shape",
          "[config][storage_shape]")
{
    SECTION("plain leaf")
    {
        auto made = nucleus::config::from_values({{"tags", "plain"}});
        REQUIRE(made);
        CHECK(made->get_all("tags") == std::vector<std::string>{"plain"});
    }

    SECTION("indexed leaf")
    {
        raw_values values;
        for(std::size_t ordinal : ordinals)
            values.emplace("tags[" + std::to_string(ordinal) + "]",
                           std::to_string(ordinal));
        auto made = nucleus::config::from_values(std::move(values));
        REQUIRE(made);
        CHECK(made->get_all("tags") == std::vector<std::string>{"0", "1", "2", "10"});
    }

    SECTION("nested indexed leaves")
    {
        raw_values values;
        for(std::size_t outer : ordinals)
            for(std::size_t inner : ordinals)
                values.emplace(
                        "cluster/node[" + std::to_string(outer) + "]/route[" + std::to_string(inner) + "]/port",
                        std::to_string((outer * 100) + inner));
        auto made = nucleus::config::from_values(std::move(values));
        REQUIRE(made);
        CHECK(made->get_all("cluster/node[2]/route/port") == std::vector<std::string>{"200", "201", "202", "210"});
    }
}

TEST_CASE("checked config construction rejects conflicting storage shapes",
          "[config][storage_shape]")
{
    SECTION("top-level conflict")
    {
        auto made = nucleus::config::from_values(
                {{"tags", "plain"}, {"tags[0]", "zero"}});
        REQUIRE_FALSE(made);
        CHECK(made.error().code == nucleus::errc::schema_violation);
        CHECK(mentions(made.error(), "canonical path 'tags'"));
        CHECK(mentions(made.error(), "concrete paths 'tags' and 'tags[0]'"));
    }

    SECTION("nested conflict across outer instances")
    {
        auto made = nucleus::config::from_values({{"cluster/node[0]/route/port", "plain"},
                                                  {"cluster/node[1]/route[0]/port", "indexed"}});
        REQUIRE_FALSE(made);
        CHECK(made.error().code == nucleus::errc::schema_violation);
        CHECK(mentions(made.error(), "canonical path 'cluster/node/route/port'"));
        CHECK(mentions(made.error(), "cluster/node[0]/route/port"));
        CHECK(mentions(made.error(), "cluster/node[1]/route[0]/port"));
    }
}

TEST_CASE("checked config construction rejects every malformed path form",
          "[config][storage_shape]")
{
    for(const std::string &path : {std::string{}, std::string("/bad"),
                                   std::string("bad/"), std::string("bad//path"),
                                   std::string("[3]"), std::string("node[]"),
                                   std::string("node[01]"), std::string("node[-1]"),
                                   std::string("node[1"), std::string("node[1]tail"),
                                   std::string("node[1234567890123456789]")})
        require_malformed_path(path);
}

TEST_CASE("resolved storage shape is independent of schema and typing",
          "[config][storage_shape][load]")
{
    require_mixed_rejection(empty_space());
    require_mixed_rejection(untyped_space());
    require_mixed_rejection(typed_space());
}
