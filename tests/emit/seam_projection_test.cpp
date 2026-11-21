#include "nucleus/config.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"

#include "nucleus/env/env_emitter.h"

#include "nucleus/argv/argv_emitter.h"

#include "nucleus/xml/xml_emitter.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <utility>

namespace {

nucleus::config_space make_server_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::enum_element(
            "mode", nucleus::anchor::keyspace("server"),
            std::vector<std::string>{"primary", "secondary"})));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config make_server_config()
{
    std::map<std::string, std::string> values{
            {"server/host", "localhost"},
            {"server/tag[0]", "alpha"},
            {"server/tag[1]", "beta"}};
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

}

TEST_CASE("env and args project a schema into flat KEY= templates", "[emit][seam]")
{
    const nucleus::config_space space = make_server_space();

    std::ostringstream env_out;
    REQUIRE(nucleus::env::emit_template(space, env_out));
    const std::string env = env_out.str();

    REQUIRE(env.find("server/host=") != std::string::npos);
    REQUIRE(env.find("server/mode=") != std::string::npos);
    REQUIRE(env.find("allowed: primary|secondary") != std::string::npos);
    REQUIRE(env.find('<') == std::string::npos);

    std::ostringstream args_out;
    REQUIRE(nucleus::argv::emit_template(space, args_out));
    const std::string args = args_out.str();
    REQUIRE(args.find("--server-host=") != std::string::npos);
    REQUIRE(args.find("--server-mode=") != std::string::npos);
    REQUIRE(args.find('<') == std::string::npos);

    const auto         delim = nucleus::cli_delimiter::parse("__").value();
    std::ostringstream custom_out;
    REQUIRE(nucleus::argv::emit_template(space, custom_out, delim));
    REQUIRE(custom_out.str().find("--server__host=") != std::string::npos);

    std::ostringstream anchored_out;
    REQUIRE(nucleus::argv::emit_template(space, anchored_out, {},
                                         nucleus::key_path::parse("server").value()));
    const std::string anchored = anchored_out.str();
    REQUIRE(anchored.find("--host=") != std::string::npos);
    REQUIRE(anchored.find("--server") == std::string::npos);
}

TEST_CASE("an anchored argv document renders keys relative to the anchor", "[emit][seam]")
{
    const nucleus::config config = make_server_config();

    std::ostringstream out;
    REQUIRE(nucleus::argv::emit_document(config, out, {},
                                         nucleus::key_path::parse("server").value()));
    const std::string args = out.str();
    REQUIRE(args.find("--host=localhost") != std::string::npos);
    REQUIRE(args.find("--tag-0=alpha") != std::string::npos);
    REQUIRE(args.find("--tag-1=beta") != std::string::npos);
    REQUIRE(args.find("--server") == std::string::npos);
}

TEST_CASE("env and args emit one flat line per resolved value", "[emit][seam]")
{
    const nucleus::config config = make_server_config();

    std::ostringstream env_out;
    REQUIRE(nucleus::env::emit_document(config, env_out));
    const std::string env = env_out.str();
    REQUIRE(env.find("server/host=localhost") != std::string::npos);
    REQUIRE(env.find("server/tag[0]=alpha") != std::string::npos);
    REQUIRE(env.find("server/tag[1]=beta") != std::string::npos);

    std::ostringstream args_out;
    REQUIRE(nucleus::argv::emit_document(config, args_out));
    const std::string args = args_out.str();
    REQUIRE(args.find("--server-tag-0=alpha") != std::string::npos);
    REQUIRE(args.find("--server-tag-1=beta") != std::string::npos);
}

TEST_CASE("xml projects the SAME space into nested tree markup", "[emit][seam]")
{
    const nucleus::config_space space = make_server_space();
    const auto                  xml   = nucleus::xml::render_template(space);
    REQUIRE(xml);
    REQUIRE(xml->find("<server>") != std::string::npos);
    REQUIRE(xml->find("<host") != std::string::npos);
    const auto made = nucleus::config::from_values(
            {{"server/host", "localhost"}});
    REQUIRE(made);
    const auto document = nucleus::xml::render_document(made.value(), space);
    REQUIRE(document);
    REQUIRE(document->find("<host>localhost</host>") != std::string::npos);
}
