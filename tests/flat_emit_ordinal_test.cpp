#include "nucleus/config.h"

#include "nucleus/env/env_emitter.h"

#include "nucleus/argv/argv_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <utility>
#include <string_view>

namespace {

nucleus::config large_ordinal_config(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::key_path anchor_of(std::string_view text)
{
    auto parsed = nucleus::key_path::parse(text);
    REQUIRE(parsed);
    return std::move(parsed).value();
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream       input(text);
    for(std::string line; std::getline(input, line);)
        lines.push_back(std::move(line));
    return lines;
}

nucleus::config boundary_config()
{
    return large_ordinal_config(
            {{"cluster/node[4294967295]/port", "a"},
             {"cluster/node[4294967296]/port", "b"},
             {"cluster/node[4294967297]/port", "c"},
             {"cluster/node[999999999999999998]/port", "d"},
             {"cluster/node[999999999999999999]/port", "e"}});
}

}

TEST_CASE("flat spelling preserves ordinals past the 32-bit boundary",
          "[flat][emit][argv][env][ordinal]")
{
    const nucleus::config config = boundary_config();

    const auto argv = nucleus::argv::render_document(config);
    REQUIRE(argv);
    REQUIRE(split_lines(argv.value())
            == std::vector<std::string>{"--cluster-node-4294967295-port=a",
                                        "--cluster-node-4294967296-port=b",
                                        "--cluster-node-4294967297-port=c",
                                        "--cluster-node-999999999999999998-port=d",
                                        "--cluster-node-999999999999999999-port=e"});

    const auto environment = nucleus::env::render_document(config);
    REQUIRE(environment);
    REQUIRE(split_lines(environment.value())
            == std::vector<std::string>{"cluster/node[4294967295]/port=a",
                                        "cluster/node[4294967296]/port=b",
                                        "cluster/node[4294967297]/port=c",
                                        "cluster/node[999999999999999998]/port=d",
                                        "cluster/node[999999999999999999]/port=e"});
}

TEST_CASE("a canonical anchor keeps every large instance and its ordinal",
          "[flat][emit][argv][anchor][ordinal]")
{
    const auto rendered = nucleus::argv::render_document(
            boundary_config(), {}, anchor_of("cluster/node"));
    REQUIRE(rendered);
    REQUIRE(split_lines(rendered.value())
            == std::vector<std::string>{"--4294967295-port=a",
                                        "--4294967296-port=b",
                                        "--4294967297-port=c",
                                        "--999999999999999998-port=d",
                                        "--999999999999999999-port=e"});
}

TEST_CASE("a concrete anchor selects one large instance and drops its ordinal",
          "[flat][emit][argv][anchor][ordinal]")
{
    const nucleus::config config = boundary_config();
    for(const std::pair<std::string, std::string> &selection :
        {std::pair<std::string, std::string>{"cluster/node[4294967296]", "b"},
         std::pair<std::string, std::string>{"cluster/node[999999999999999999]", "e"}})
    {
        const auto rendered = nucleus::argv::render_document(
                config, {}, anchor_of(selection.first));
        REQUIRE(rendered);
        REQUIRE(rendered.value() == "--port=" + selection.second + "\n");
    }
}

TEST_CASE("nested large ordinal tuples order by value, not by decimal text",
          "[flat][emit][argv][env][ordinal]")
{
    const nucleus::config config = large_ordinal_config(
            {{"cluster/node[4294967296]/route[4294967296]/port", "y"},
             {"cluster/node[4294967296]/route[4294967295]/port", "x"},
             {"cluster/node[4294967295]/route[4294967297]/port", "z"}});

    const auto argv = nucleus::argv::render_document(config);
    REQUIRE(argv);
    REQUIRE(split_lines(argv.value())
            == std::vector<std::string>{
                       "--cluster-node-4294967295-route-4294967297-port=z",
                       "--cluster-node-4294967296-route-4294967295-port=x",
                       "--cluster-node-4294967296-route-4294967296-port=y"});

    const auto environment = nucleus::env::render_document(config);
    REQUIRE(environment);
    REQUIRE(split_lines(environment.value())
            == std::vector<std::string>{
                       "cluster/node[4294967295]/route[4294967297]/port=z",
                       "cluster/node[4294967296]/route[4294967295]/port=x",
                       "cluster/node[4294967296]/route[4294967296]/port=y"});
}
