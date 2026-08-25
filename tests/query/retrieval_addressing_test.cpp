#include "nucleus/config.h"
#include "support/builder_result_test_support.h"
#include "nucleus/error.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/argv/argv_source.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace {

constexpr std::array<std::size_t, 4> ordinals{0, 1, 2, 10};

nucleus::config_space retrieval_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::typed_element<std::int32_t>(
            "version", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::repeated_element(
            "node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::repeated_element(
            "route", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(nucleus::typed_element<std::int32_t>(
            "port", nucleus::anchor::keyspace("cluster/node/route"))));
    return nucleus::builder_result_test::built(builder);
}

std::int32_t encoded(std::size_t outer, std::size_t inner)
{
    return static_cast<std::int32_t>((outer * 100) + inner);
}

nucleus::config sparse_matrix()
{
    nucleus::runtime_source source;
    source.set("cluster/version", "7");
    for(std::size_t const outer : ordinals)
        for(std::size_t const inner : ordinals)
            source.set("cluster/node[" + std::to_string(outer) + "]/route[" + std::to_string(inner) + "]/port",
                       std::to_string(encoded(outer, inner)));
    auto loaded = nucleus::load_config(
            retrieval_space(), nucleus::source_stack{std::move(source)}, {});
    REQUIRE(loaded);
    return std::move(loaded).value();
}

std::vector<std::int32_t> expected_values(
        const std::vector<std::size_t> &outer_values,
        const std::vector<std::size_t> &inner_values)
{
    std::vector<std::int32_t> values;
    for(std::size_t const outer : outer_values)
        for(std::size_t const inner : inner_values)
            values.push_back(encoded(outer, inner));
    return values;
}

void check_gather(const nucleus::config &config, const std::string &path,
                  const std::vector<std::int32_t> &expected)
{
    std::vector<std::string> raw;
    raw.reserve(expected.size());
    for(std::int32_t const value : expected)
        raw.push_back(std::to_string(value));
    CHECK(config.get_all(path) == raw);
    auto typed = config.get_all_as<std::int32_t>(path);
    REQUIRE(typed);
    CHECK(typed.value() == expected);
}

nucleus::xml_source xml_base()
{
    std::string document = "<cluster><version>7</version><node>";
    for(std::size_t inner = 0; inner <= 10; ++inner)
        document += "<route><port>" + std::to_string(encoded(0, inner)) + "</port></route>";
    document += "</node><node><route><port>5000</port></route></node></cluster>";
    return nucleus::xml_source::from(
            nucleus::xml_source_options::of_string(document));
}

std::string override_flag(std::size_t inner, std::int32_t value)
{
    return "--cluster-node-0-route-" + std::to_string(inner) + "-port=" + std::to_string(value);
}

}

TEST_CASE("raw and typed gathers obey every explicit ordinal and full tuple order",
          "[retrieval][ordinal]")
{
    const nucleus::config          config = sparse_matrix();
    const std::vector<std::size_t> all(ordinals.begin(), ordinals.end());
    check_gather(config, "cluster/node/route/port", expected_values(all, all));
    check_gather(config, "cluster/node/route[10]/port", expected_values(all, {10}));
    for(std::size_t const outer : ordinals)
    {
        const std::string node = "cluster/node[" + std::to_string(outer) + "]";
        check_gather(config, node + "/route/port", expected_values({outer}, all));
        for(std::size_t const inner : ordinals)
            check_gather(config, node + "/route[" + std::to_string(inner) + "]/port",
                         {encoded(outer, inner)});
    }
    check_gather(config, "cluster/version", {7});
    CHECK(config.get_all("cluster/node[0]/route/missing").empty());
    auto missing = config.get_all_as<std::int32_t>(
            "cluster/node[0]/route/missing");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == nucleus::errc::absent_key);
}

TEST_CASE("later same-rank nested CLI ordinals win with their provenance",
          "[retrieval][argv][precedence]")
{
    const nucleus::config_space space = retrieval_space();
    for(const auto &[first, second] :
        std::array<std::pair<std::int32_t, std::int32_t>, 2>{
                std::pair{9002, 8002}, std::pair{8002, 9002}})
    {
        nucleus::argv_source argv({override_flag(2, first), override_flag(2, second)});
        argv.recognize_with(nucleus::recognizer_of(space));
        auto loaded = nucleus::load_config(
                space, nucleus::source_stack{xml_base(), std::move(argv)}, {});
        REQUIRE(loaded);
        auto gathered = loaded->get_all_as<std::int32_t>(
                "cluster/node[0]/route/port");
        REQUIRE(gathered);
        REQUIRE(gathered->size() == 11);
        CHECK((*gathered)[2] == second);
        const nucleus::origin *winner = loaded->provenance_of(
                "cluster/node[0]/route[2]/port");
        REQUIRE(winner != nullptr);
        CHECK(winner->rank == 1);
        CHECK(winner->layer == "stack[1]");
    }
}

TEST_CASE("deferred CLI ordinal precedence preserves rank and range",
          "[retrieval][argv][precedence]")
{
    const nucleus::config_space space = retrieval_space();
    nucleus::argv_source        lower({override_flag(2, 9002)});
    lower.recognize_with(nucleus::recognizer_of(space));
    auto lower_result = nucleus::load_config(
            space, nucleus::source_stack{std::move(lower), xml_base()}, {});
    REQUIRE(lower_result);
    CHECK(lower_result->get("cluster/node[0]/route[2]/port") == "2");
    const nucleus::origin *lower_winner = lower_result->provenance_of(
            "cluster/node[0]/route[2]/port");
    REQUIRE(lower_winner != nullptr);
    CHECK(lower_winner->rank == 1);

    nucleus::argv_source out_of_range({override_flag(11, 9011)});
    out_of_range.recognize_with(nucleus::recognizer_of(space));
    auto rejected = nucleus::load_config(
            space, nucleus::source_stack{xml_base(), std::move(out_of_range)}, {});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == nucleus::errc::schema_violation);
    CHECK(rejected.error().message.find("out of range") != std::string::npos);
}
