#include "nucleus/config_emitter.h"
#include "nucleus/config.h"
#include "nucleus/config_space.h"

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
#include <utility>

// The config_emitter seam is genuinely multi-format: env and args are honest FLAT
// KEY= sources, while xml is a tree format -- and all three project the SAME space
// (template) and config (document). The format is what differs, not the data.

// Compile-time proof: each stateless emitter models the core concept.
static_assert(nucleus::config_emitter<nucleus::env::emitter>);
static_assert(nucleus::config_emitter<nucleus::argv::emitter>);
static_assert(nucleus::config_emitter<nucleus::xml::emitter>);

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
    return builder.build();
}

// A config carrying a scalar and a repeated collection, built directly so the
// document emitters can be proven without a source dependency.
// Repeated values are stored as indexed scalars per the unified storage model.
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

    // Flat leaf paths are present, the container is not a line of its own, the
    // constrained leaf is annotated, and there is no tree markup.
    REQUIRE(env.find("server/host=") != std::string::npos);
    REQUIRE(env.find("server/mode=") != std::string::npos);
    REQUIRE(env.find("allowed: primary|secondary") != std::string::npos);
    REQUIRE(env.find('<') == std::string::npos);

    // The argv template lines are REAL flags: the key joined by the delimiter,
    // exactly what argv_source parses back.
    std::ostringstream args_out;
    REQUIRE(nucleus::argv::emit_template(space, args_out));
    const std::string args = args_out.str();
    REQUIRE(args.find("--server-host=") != std::string::npos);
    REQUIRE(args.find("--server-mode=") != std::string::npos);
    REQUIRE(args.find('<') == std::string::npos);

    // A custom delimiter re-renders the same surface under the host's grammar.
    const auto delim = nucleus::cli_delimiter::parse("__").value();
    std::ostringstream custom_out;
    REQUIRE(nucleus::argv::emit_template(space, custom_out, delim));
    REQUIRE(custom_out.str().find("--server__host=") != std::string::npos);

    // An anchored grammar drops the never-changing root from every flag.
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

    std::ostringstream xml_out;
    REQUIRE(nucleus::xml::emit_template(space, xml_out));
    const std::string xml = xml_out.str();

    // The same data the flat sources rendered as KEY= lines becomes a nested tree.
    REQUIRE(xml.find("<server>") != std::string::npos);
    REQUIRE(xml.find("<host") != std::string::npos);
}

namespace {

// Emits a one-key config and hands back both the result channel and whatever
// reached the stream, so a test can assert all-or-nothing on failure.
std::pair<nucleus::expected<void, nucleus::error>, std::string>
emit_one(const std::string &key, const std::string &value)
{
    std::map<std::string, std::string> values{{key, value}};
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    const nucleus::config cfg = std::move(made).value();
    std::ostringstream out;
    auto result = nucleus::xml::emit_document(cfg, out);
    return {std::move(result), out.str()};
}

}

TEST_CASE("xml emit rejects a key segment that is not a valid XML name",
          "[emit][seam][xml]")
{
    // Every hostile ASCII case the write boundary must catch, as an element name
    // (leading segment) and as a leaf name.
    const std::vector<std::string> hostile_keys{
        "ser ver/host",   // space
        "server/ho st",   // space in the leaf
        "ser<ver/host",   // '<'
        "server/ho\"st",  // '"'
        "9server/host",   // leading digit
        "server/0host"};  // leading digit in the leaf

    for(const std::string &key : hostile_keys)
    {
        auto [result, emitted] = emit_one(key, "value");
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == nucleus::errc::malformed_source);
        // All-or-nothing: nothing reached the stream.
        REQUIRE(emitted.empty());
    }
}

TEST_CASE("xml emit rejects a value carrying a control byte or a bare CR",
          "[emit][seam][xml]")
{
    for(const std::string &value : {std::string("a\x01""b"), std::string("a\rb")})
    {
        auto [result, emitted] = emit_one("server/host", value);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == nucleus::errc::malformed_source);
        REQUIRE(emitted.empty());
    }
}

TEST_CASE("xml emit accepts a high-byte (UTF-8) name and a newline-bearing value",
          "[emit][seam][xml]")
{
    // A legal international element name is permitted un-decoded.
    auto [name_ok, name_out] = emit_one("caf\xc3\xa9/host", "value");
    REQUIRE(name_ok);
    REQUIRE(name_out.find("caf\xc3\xa9") != std::string::npos);

    // Tab and newline are legal XML characters and round-trip-stable, so a value
    // carrying them still emits (only bare CR and other C0 controls are refused).
    auto [value_ok, value_out] = emit_one("server/host", "line1\nline2\ttab");
    REQUIRE(value_ok);
    REQUIRE(value_out.find("line1") != std::string::npos);
}
