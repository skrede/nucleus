#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/env/env_source.h"
#include "nucleus/env/env_emitter.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/argv_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <utility>
#include <string_view>

namespace {

nucleus::config checked_config(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::config ordinal_config()
{
    return checked_config({{"cluster/node[0]/port", "8000"},
                           {"cluster/node[1]/port", "9000"},
                           {"cluster/node[2]/port", "10000"},
                           {"cluster/node[10]/port", "18000"}});
}

nucleus::key_path path_of(std::string_view text)
{
    auto parsed = nucleus::key_path::parse(text);
    REQUIRE(parsed);
    return std::move(parsed).value();
}

std::vector<std::string> lines_of(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream       input(text);
    for(std::string line; std::getline(input, line);)
        lines.push_back(std::move(line));
    return lines;
}

nucleus::config_space repeated_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::element("port", nucleus::anchor::keyspace("cluster/node"))));
    return builder.build();
}

}

TEST_CASE("owned argv rendering retains concrete ordinals in numeric order",
          "[flat][emit][argv][ordinal]")
{
    const auto argv_template = nucleus::argv::render_template(repeated_space());
    const auto env_template  = nucleus::env::render_template(repeated_space());
    REQUIRE(argv_template);
    REQUIRE(argv_template->find("--cluster-node-port=") != std::string::npos);
    REQUIRE(env_template);
    REQUIRE(env_template->find("cluster/node/port=") != std::string::npos);
    const auto rendered = nucleus::argv::render_document(ordinal_config());
    REQUIRE(rendered);
    REQUIRE(lines_of(rendered.value()) == std::vector<std::string>{"--cluster-node-0-port=8000", "--cluster-node-1-port=9000", "--cluster-node-2-port=10000", "--cluster-node-10-port=18000"});
}

TEST_CASE("owned argv output reparses to the same concrete instances",
          "[flat][emit][argv][round_trip]")
{
    const nucleus::config original = checked_config({{"cluster/node[0]/port", "8000"},
                                                     {"cluster/node[1]/port", "9000"}});
    const auto            rendered = nucleus::argv::render_document(original);
    REQUIRE(rendered);

    nucleus::runtime_source base;
    base.set("cluster/node[0]/port", "80")
            .set("cluster/node[1]/port", "90");
    nucleus::argv_source        replay(lines_of(rendered.value()));
    const nucleus::config_space space = repeated_space();
    replay.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(base), std::move(replay)}, {});
    REQUIRE(loaded);
    REQUIRE(loaded->keys() == original.keys());
    REQUIRE(loaded->get("cluster/node[0]/port") == "8000");
    REQUIRE(loaded->get("cluster/node[1]/port") == "9000");
}

TEST_CASE("structural anchors select ordinary and repeated descendants",
          "[flat][emit][argv][anchor]")
{
    const nucleus::config config   = ordinal_config();
    const auto            ordinary = nucleus::argv::render_document(
            config, {}, path_of("cluster"));
    REQUIRE(ordinary);
    REQUIRE(lines_of(ordinary.value()) == std::vector<std::string>{"--node-0-port=8000", "--node-1-port=9000", "--node-2-port=10000", "--node-10-port=18000"});

    const auto canonical = nucleus::argv::render_document(
            config, {}, path_of("cluster/node"));
    REQUIRE(canonical);
    REQUIRE(lines_of(canonical.value()) == std::vector<std::string>{"--0-port=8000", "--1-port=9000", "--2-port=10000", "--10-port=18000"});

    const auto concrete = nucleus::argv::render_document(
            config, {}, path_of("cluster/node[1]"));
    REQUIRE(concrete);
    REQUIRE(concrete.value() == "--port=9000\n");
}

TEST_CASE("flat rendering preserves complete nested ordinal tuples",
          "[flat][emit][argv][env][ordinal]")
{
    const nucleus::config config = checked_config({{"cluster/node[0]/route[10]/port", "a"},
                                                   {"cluster/node[1]/route[2]/port", "b"},
                                                   {"cluster/node[1]/route[10]/port", "c"}});
    const auto            argv   = nucleus::argv::render_document(config);
    REQUIRE(argv);
    REQUIRE(lines_of(argv.value()) == std::vector<std::string>{"--cluster-node-0-route-10-port=a", "--cluster-node-1-route-2-port=b", "--cluster-node-1-route-10-port=c"});

    const auto environment = nucleus::env::render_document(config);
    REQUIRE(environment);
    REQUIRE(lines_of(environment.value()) == std::vector<std::string>{"cluster/node[0]/route[10]/port=a", "cluster/node[1]/route[2]/port=b", "cluster/node[1]/route[10]/port=c"});
    nucleus::env_source replay;
    for(const std::string &line : lines_of(environment.value()))
    {
        const std::size_t split = line.find('=');
        REQUIRE(split != std::string::npos);
        replay.set(line.substr(0, split), line.substr(split + 1));
    }
    auto pulled = replay.pull();
    REQUIRE(pulled);
    REQUIRE(pulled->entries.front().path == "cluster/node[0]/route[10]/port");
}

TEST_CASE("flat validation is scoped to the selected subtree",
          "[flat][emit][argv][anchor][error]")
{
    const nucleus::config config   = checked_config({{"cluster/port", "8000"},
                                                     {std::string("outside/bad-key"), "ignored"}});
    const auto            selected = nucleus::argv::render_document(
            config, {}, path_of("cluster"));
    REQUIRE(selected);
    REQUIRE(selected.value() == "--port=8000\n");

    const auto invalid = nucleus::argv::render_document(
            config, {}, path_of("outside"));
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error().code == nucleus::errc::malformed_source);
    REQUIRE(invalid.error().message.find("bad-key") != std::string::npos);
}

TEST_CASE("a scalar anchor fails before stream output",
          "[flat][emit][argv][anchor][error]")
{
    const nucleus::config config = checked_config({{"cluster/port", "8000"}});
    std::ostringstream    output;
    const auto            result = nucleus::argv::emit_document(
            config, output, {}, path_of("cluster/port"));
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("cluster/port") != std::string::npos);
    REQUIRE(output.str().empty());
}

TEST_CASE("selected line-breaking values fail before stream output",
          "[flat][emit][env][error]")
{
    for(const std::string &value : {std::string("line\nbreak"),
                                    std::string("line\rbreak")})
    {
        const nucleus::config config = checked_config({{"cluster/port", value}});
        std::ostringstream    output;
        const auto            result = nucleus::env::emit_document(config, output);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == nucleus::errc::malformed_source);
        REQUIRE(result.error().message.find("cluster/port") != std::string::npos);
        REQUIRE(output.str().empty());
    }
}
