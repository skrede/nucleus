#include "nucleus/config_emitter.h"
#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/env/env_emitter.h"
#include "nucleus/argv/argv_emitter.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <sstream>

// The config_emitter seam is genuinely multi-format: env and args are honest FLAT
// KEY= sources, while xml is a tree format -- and all three project the SAME space
// (template) and configuration (document). The format is what differs, not the data.

// Compile-time proof: each stateless emitter models the core concept.
static_assert(nucleus::config_emitter<nucleus::env::emitter>);
static_assert(nucleus::config_emitter<nucleus::argv::emitter>);
static_assert(nucleus::config_emitter<nucleus::xml::emitter>);

namespace {

[[nodiscard]] nucleus::configuration_space make_server_space()
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server")));
    builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server")));
    builder.register_element(nucleus::enum_element(
        "mode", nucleus::anchor::keyspace("server"),
        std::vector<std::string>{"primary", "secondary"}));
    return builder.build();
}

// A configuration carrying a scalar and a repeated collection, built directly so the
// document emitters can be proven without a source dependency.
[[nodiscard]] nucleus::configuration make_server_config()
{
    std::map<std::string, std::string> values{{"server/host", "localhost"}};
    std::map<std::string, std::vector<std::string>> collections{
        {"server/tag", {"alpha", "beta"}}};
    return nucleus::configuration(std::move(values), std::move(collections), nucleus::provenance{});
}

[[nodiscard]] std::size_t count_occurrences(const std::string &text, const std::string &needle)
{
    std::size_t count = 0;
    for(std::size_t pos = text.find(needle); pos != std::string::npos;
        pos = text.find(needle, pos + needle.size()))
        ++count;
    return count;
}

}

TEST_CASE("env and args project a schema into flat KEY= templates", "[emit][seam]")
{
    const nucleus::configuration_space space = make_server_space();

    std::ostringstream env_out;
    nucleus::env::emit_template(space, env_out);
    const std::string env = env_out.str();

    // Flat leaf paths are present, the container is not a line of its own, the
    // constrained leaf is annotated, and there is no tree markup.
    REQUIRE(env.find("server/host=") != std::string::npos);
    REQUIRE(env.find("server/mode=") != std::string::npos);
    REQUIRE(env.find("allowed: primary|secondary") != std::string::npos);
    REQUIRE(env.find('<') == std::string::npos);

    std::ostringstream args_out;
    nucleus::argv::emit_template(space, args_out);
    const std::string args = args_out.str();
    REQUIRE(args.find("--server/host=") != std::string::npos);
    REQUIRE(args.find("--server/mode=") != std::string::npos);
    REQUIRE(args.find('<') == std::string::npos);
}

TEST_CASE("env and args emit one flat line per resolved value", "[emit][seam]")
{
    const nucleus::configuration config = make_server_config();

    std::ostringstream env_out;
    nucleus::env::emit_document(config, env_out);
    const std::string env = env_out.str();
    REQUIRE(env.find("server/host=localhost") != std::string::npos);
    // The repeated path persists ALL its values, one line each (no last-wins loss).
    REQUIRE(count_occurrences(env, "server/tag=") == 2);
    REQUIRE(env.find("server/tag=alpha") != std::string::npos);
    REQUIRE(env.find("server/tag=beta") != std::string::npos);

    std::ostringstream args_out;
    nucleus::argv::emit_document(config, args_out);
    const std::string args = args_out.str();
    REQUIRE(count_occurrences(args, "--server/tag=") == 2);
}

TEST_CASE("xml projects the SAME space into nested tree markup", "[emit][seam]")
{
    const nucleus::configuration_space space = make_server_space();

    std::ostringstream xml_out;
    nucleus::xml::emit_template(space, xml_out);
    const std::string xml = xml_out.str();

    // The same data the flat sources rendered as KEY= lines becomes a nested tree.
    REQUIRE(xml.find("<server>") != std::string::npos);
    REQUIRE(xml.find("<host") != std::string::npos);
}
