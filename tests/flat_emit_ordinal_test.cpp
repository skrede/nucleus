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
            {{"cluster/node[9]/port", "a"},
             {"cluster/node[10]/port", "b"},
             {"cluster/node[4294967293]/port", "c"},
             {"cluster/node[4294967294]/port", "d"},
             {"cluster/node[4294967295]/port", "e"}});
}

}

TEST_CASE("flat spelling preserves every ordinal up to the accepted bound",
          "[flat][emit][argv][env][ordinal]")
{
    const nucleus::config config = boundary_config();

    const auto argv = nucleus::argv::render_document(config);
    REQUIRE(argv);
    REQUIRE(split_lines(argv.value())
            == std::vector<std::string>{"--cluster-node-9-port=a",
                                        "--cluster-node-10-port=b",
                                        "--cluster-node-4294967293-port=c",
                                        "--cluster-node-4294967294-port=d",
                                        "--cluster-node-4294967295-port=e"});

    const auto environment = nucleus::env::render_document(config);
    REQUIRE(environment);
    REQUIRE(split_lines(environment.value())
            == std::vector<std::string>{"cluster/node[9]/port=a",
                                        "cluster/node[10]/port=b",
                                        "cluster/node[4294967293]/port=c",
                                        "cluster/node[4294967294]/port=d",
                                        "cluster/node[4294967295]/port=e"});
}

TEST_CASE("a canonical anchor keeps every large instance and its ordinal",
          "[flat][emit][argv][anchor][ordinal]")
{
    const auto rendered = nucleus::argv::render_document(
            boundary_config(), {}, anchor_of("cluster/node"));
    REQUIRE(rendered);
    REQUIRE(split_lines(rendered.value())
            == std::vector<std::string>{"--9-port=a",
                                        "--10-port=b",
                                        "--4294967293-port=c",
                                        "--4294967294-port=d",
                                        "--4294967295-port=e"});
}

TEST_CASE("a concrete anchor selects one large instance and drops its ordinal",
          "[flat][emit][argv][anchor][ordinal]")
{
    const nucleus::config config = boundary_config();
    for(const std::pair<std::string, std::string> &selection :
        {std::pair<std::string, std::string>{"cluster/node[10]", "b"},
         std::pair<std::string, std::string>{"cluster/node[4294967295]", "e"}})
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
            {{"cluster/node[4294967294]/route[4294967294]/port", "y"},
             {"cluster/node[4294967294]/route[4294967293]/port", "x"},
             {"cluster/node[4294967293]/route[4294967295]/port", "z"}});

    const auto argv = nucleus::argv::render_document(config);
    REQUIRE(argv);
    REQUIRE(split_lines(argv.value())
            == std::vector<std::string>{
                       "--cluster-node-4294967293-route-4294967295-port=z",
                       "--cluster-node-4294967294-route-4294967293-port=x",
                       "--cluster-node-4294967294-route-4294967294-port=y"});

    const auto environment = nucleus::env::render_document(config);
    REQUIRE(environment);
    REQUIRE(split_lines(environment.value())
            == std::vector<std::string>{
                       "cluster/node[4294967293]/route[4294967295]/port=z",
                       "cluster/node[4294967294]/route[4294967293]/port=x",
                       "cluster/node[4294967294]/route[4294967294]/port=y"});
}

TEST_CASE("an ordinal above the accepted bound is reported, never rendered",
          "[flat][emit][ordinal]")
{
    REQUIRE(nucleus::config::from_values({{"cluster/node[4294967295]/port", "a"}}));

    for(const std::string &above : {std::string("cluster/node[4294967296]/port"),
                                    std::string("cluster/node[9999999999]/port"),
                                    std::string("cluster/node[999999999999999999]/port")})
    {
        const auto made = nucleus::config::from_values({{above, "a"}});
        REQUIRE_FALSE(made);
        REQUIRE(made.error().code == nucleus::errc::malformed_source);
        REQUIRE(made.error().message.find(above) != std::string::npos);
    }
}
