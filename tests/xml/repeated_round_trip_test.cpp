#include "xml/repeated_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace test = nucleus::xml_repeated_test;

namespace {

void check_twelve_ports(const nucleus::config &config)
{
    const std::vector<std::string> ports = config.get_all("cluster/node/port");
    REQUIRE(ports.size() == 12);
    for(std::int32_t ordinal = 0; ordinal < 12; ++ordinal)
    {
        const std::string path =
                "cluster/node[" + std::to_string(ordinal) + "]/port";
        const std::string expected = std::to_string(ordinal * 10);
        CHECK(config.get(path) == expected);
        CHECK(ports[static_cast<std::size_t>(ordinal)] == expected);
    }
}

}

TEST_CASE("xml emitter -- repeated container bracket strip", "[xml][xml_emitter]")
{
    const nucleus::config_space space = test::cluster_space();
    const std::string           xml =
            "<cluster>"
            "<node><port>1.5</port><metrics><latency>0.1</latency></metrics></node>"
            "<node><port>2.0</port><metrics><latency>0.2</latency></metrics></node>"
            "</cluster>";
    const auto loaded = nucleus::load_config(
            space, nucleus::source_stack{}, test::document_options(xml));
    REQUIRE(loaded);

    const auto rendered = nucleus::xml::render_document(loaded.value(), space);
    REQUIRE(rendered);
    CHECK(rendered->find("node[0]") == std::string::npos);
    CHECK(rendered->find("node[1]") == std::string::npos);
    CHECK(rendered->find("1.5") != std::string::npos);
    CHECK(rendered->find("2.0") != std::string::npos);
    CHECK(rendered->find("0.1") != std::string::npos);
    CHECK(rendered->find("0.2") != std::string::npos);
    REQUIRE(rendered->find("<node>") != rendered->rfind("<node>"));
}

TEST_CASE("xml emitter -- repeated container round-trip",
          "[xml][xml_emitter][round_trip]")
{
    const nucleus::config_space space = test::cluster_space();
    const std::string           xml =
            "<cluster>"
            "<node><port>1.5</port><metrics><latency>0.1</latency></metrics></node>"
            "<node><port>2.0</port><metrics><latency>0.2</latency></metrics></node>"
            "</cluster>";
    const auto original = nucleus::load_config(
            space, nucleus::source_stack{}, test::document_options(xml));
    REQUIRE(original);

    const auto rendered = nucleus::xml::render_document(original.value(), space);
    REQUIRE(rendered);
    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{}, test::document_options(rendered.value()));
    REQUIRE(reloaded);
    CHECK(reloaded->get("cluster/node[0]/port") == "1.5");
    CHECK(reloaded->get("cluster/node[1]/port") == "2.0");
    CHECK(reloaded->get("cluster/node[0]/metrics/latency") == "0.1");
    CHECK(reloaded->get("cluster/node[1]/metrics/latency") == "0.2");
}

TEST_CASE("xml emitter -- repeated container round-trip with N >= 11 instances",
          "[xml][xml_emitter][round_trip][multi_digit_ordinal]")
{
    const nucleus::config_space space = test::simple_cluster_space();
    nucleus::runtime_source     source;
    for(std::int32_t ordinal = 0; ordinal < 12; ++ordinal)
        source.set("cluster/node[" + std::to_string(ordinal) + "]/port",
                   std::to_string(ordinal * 10));
    const auto original = nucleus::load_config(
            space, nucleus::source_stack{std::move(source)}, {});
    REQUIRE(original);

    const auto rendered = nucleus::xml::render_document(original.value(), space);
    REQUIRE(rendered);
    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{}, test::document_options(rendered.value()));
    REQUIRE(reloaded);
    check_twelve_ports(reloaded.value());
}
