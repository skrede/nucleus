#include "support/builder_result_test_support.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using nucleus::anchor;
using nucleus::config_space;
using nucleus::config_space_builder;
using nucleus::key_path;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

bool holds(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

void register_described(config_space_builder &engine, const char *name, const char *description)
{
    REQUIRE(engine.register_element(nucleus::described(nucleus::element(name, anchor::keyspace(path_of("logging"))), description)));
}

config_space described_space()
{
    config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", anchor::root())));
    register_described(engine, "clear", "a\x1b[2Jb");
    register_described(engine, "wrap", "first\nsecond");
    register_described(engine, "csi", "c\xc2\x9bz");
    register_described(engine, "bidi", "e\xe2\x80\xaeg");
    register_described(engine, "level", "set the logging level");
    register_described(engine, "cafe", "café");
    register_described(engine, "size", "größe");
    REQUIRE(engine.register_element(
            nucleus::enum_element("unit", anchor::keyspace(path_of("logging")), {"has space", "größe"})));
    return nucleus::builder_result_test::built(engine);
}

}

TEST_CASE("help under a concrete repeated anchor lists the anchored flags", "[facade][help]")
{
    config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace(path_of("cluster/node")))));
    const config_space space = nucleus::builder_result_test::built(engine);

    const std::string help = space.generate_help("mytool", {}, path_of("cluster/node[1]"));

    REQUIRE(holds(help, "  --port\n"));
    REQUIRE_FALSE(holds(help, "--logging"));
    REQUIRE_FALSE(holds(help, "--cluster"));
}

TEST_CASE("a control character in a description prints as an escape", "[facade][help]")
{
    const std::string help = described_space().generate_help("mytool");

    REQUIRE(holds(help, "  --logging-clear  a\\x1b[2Jb\n"));
    REQUIRE(holds(help, "  --logging-wrap  first\\nsecond\n"));
    REQUIRE(holds(help, "  --logging-csi  c\\xc2\\x9bz\n"));
    REQUIRE(holds(help, "  --logging-bidi  e\\xe2\\x80\\xaeg\n"));
    for(const std::string &raw : std::vector<std::string>{"\x1b", "first\n", "\xc2\x9b", "\xe2\x80\xae"})
        REQUIRE_FALSE(holds(help, raw));
}

TEST_CASE("ordinary prose and values in help print unchanged", "[facade][help]")
{
    const std::string help = described_space().generate_help("mytool");

    REQUIRE(holds(help, "  --logging-level  set the logging level\n"));
    REQUIRE(holds(help, "  --logging-cafe  café\n"));
    REQUIRE(holds(help, "  --logging-size  größe\n"));
    REQUIRE(holds(help, "  --logging-unit [values: has space, größe]\n"));
}

TEST_CASE("a control character in the program name prints as an escape", "[facade][help]")
{
    const std::string help = described_space().generate_help("x\x1b]0;t\x07");

    REQUIRE(help.starts_with("x\\x1b]0;t\\x07 options:\n"));
    REQUIRE_FALSE(holds(help, "\x07"));
}
