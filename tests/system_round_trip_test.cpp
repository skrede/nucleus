#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/env/env_source.h"
#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"
#include "nucleus/runtime/runtime_source.h"

#include "nucleus/env/env_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <sstream>

// System-level round-trip proof: build a configuration in code, emit it, reload it,
// and ASSERT (not merely print) that the reloaded configuration equals the original.
//
// Shape: a runtime_source supplies scalar values; an XML overlay supplies a repeated
// field (a flat source can carry at most one value per repeated field per layer, so
// the duplicate_keys-capable XML source is needed for the collection). load() unifies
// them into C1. C1 is emitted through the XML emitter (config_emitter seam) into an
// std::ostringstream. The emitted XML is reloaded via xml_source into C2. Assertions:
//   - C2 key set == C1 key set
//   - C2 scalar values == C1 scalar values
//   - C2 repeated-field multiplicity and values == C1

using namespace nucleus;

namespace {

source_handle xml_of(const std::string &text)
{
    return source_handle(
        xml_source::from(xml_source_options::of_string(text)));
}

// Schema: server with host, mode, and a repeated tag leaf.
configuration_space make_space()
{
    configuration_space_builder builder;
    builder.register_element(element("server", anchor::root()));
    builder.register_element(element("host", anchor::keyspace("server")));
    builder.register_element(
        enum_element("mode", anchor::keyspace("server"),
                     std::vector<std::string>{"primary", "secondary"}));
    builder.register_element(repeated_element("tag", anchor::keyspace("server")));
    return builder.build();
}

}

TEST_CASE("round-trip: runtime_source + XML repeated field -> emit -> reload is lossless",
          "[system][round_trip]")
{
    const configuration_space space = make_space();

    // The scalar base, built in code.
    runtime_source base;
    base.set("server/host", "localhost")
        .set("server/mode", "primary");

    // The repeated tag values arrive from a document overlay.
    const char *doc = "<server><tag>alpha</tag><tag>beta</tag></server>";
    auto make_doc = [doc](const std::string &) -> source_handle {
        return xml_of(doc);
    };

    // runtime_source at lower precedence; document at higher via load_options.
    load_options opts;
    opts.document_paths = {"config.xml"};
    opts.make_document  = make_doc;

    auto first = load(space, source_stack{std::move(base)}, opts);
    REQUIRE(first);
    const configuration &c1 = first.value();

    // Emit C1 through the XML emitter into an in-memory stream.
    std::ostringstream emitted;
    xml::emit_document(c1, emitted);

    REQUIRE_FALSE(emitted.str().empty());

    // Reload the emitted XML as C2.
    const std::string emitted_xml = emitted.str();
    load_options reload_opts;
    reload_opts.document_paths = {"emitted.xml"};
    reload_opts.make_document  = [&emitted_xml](const std::string &) -> source_handle {
        return xml_of(emitted_xml);
    };

    auto second = load(space, source_stack{}, reload_opts);
    REQUIRE(second);
    const configuration &c2 = second.value();

    // ASSERT equivalence -- not merely print.

    // Same key set.
    REQUIRE(c2.keys() == c1.keys());

    // Same scalar values for every key.
    REQUIRE(c2.get("server/host") == c1.get("server/host"));
    REQUIRE(c2.get("server/mode") == c1.get("server/mode"));

    // Same repeated-field multiplicity and values.
    const std::vector<std::string> tags_c1 = c1.get_all("server/tag");
    const std::vector<std::string> tags_c2 = c2.get_all("server/tag");
    REQUIRE(tags_c2.size() == tags_c1.size());
    REQUIRE(tags_c2 == tags_c1);

    // The repeated leaf preserved ALL its values, not just the last.
    REQUIRE(tags_c1 == std::vector<std::string>{"alpha", "beta"});
    REQUIRE(tags_c2 == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("round-trip: all scalar values survive emit -> reload unchanged",
          "[system][round_trip]")
{
    const configuration_space space = make_space();

    runtime_source src;
    src.set("server/host", "edge-node")
       .set("server/mode", "secondary");

    // No repeated field -- purely scalar round-trip.
    auto first = load(space, source_stack{std::move(src)}, {});
    REQUIRE(first);
    const configuration &c1 = first.value();

    std::ostringstream emitted;
    xml::emit_document(c1, emitted);
    REQUIRE_FALSE(emitted.str().empty());

    const std::string xml_text = emitted.str();
    load_options reload_opts;
    reload_opts.document_paths = {"out.xml"};
    reload_opts.make_document  = [&xml_text](const std::string &) -> source_handle {
        return xml_of(xml_text);
    };

    auto second = load(space, source_stack{}, reload_opts);
    REQUIRE(second);
    const configuration &c2 = second.value();

    // Key sets match.
    REQUIRE(c2.keys() == c1.keys());

    // Every key's value survives the round-trip.
    for(const std::string &key : c1.keys())
        REQUIRE(c2.get_all(key) == c1.get_all(key));
}

TEST_CASE("round-trip via env emitter: scalar subset reloads its keys",
          "[system][round_trip]")
{
    // Round-trip a scalar-only configuration through the env emitter (flat KEY=value
    // format) and reload it via env_source to assert the env format is lossless for
    // scalars. The schema uses flat (root-level) keys so that the capability-flat
    // env_source can satisfy the gate; nested schemas require nesting capability.
    configuration_space_builder builder;
    builder.register_element(element("host", anchor::root()));
    builder.register_element(element("port", anchor::root()));
    const configuration_space space = builder.build();

    env_source src;
    src.set("host", "rt-host")
       .set("port", "5050");

    auto first = load(space, source_stack{std::move(src)}, {});
    REQUIRE(first);
    const configuration &c1 = first.value();

    // Emit via env emitter.
    std::ostringstream env_out;
    env::emit_document(c1, env_out);
    const std::string env_text = env_out.str();
    REQUIRE_FALSE(env_text.empty());

    // Parse the emitted env text back into an env_source by scanning KEY=value lines.
    env_source reloaded_env;
    std::istringstream lines(env_text);
    std::string line;
    while(std::getline(lines, line))
    {
        if(line.empty() || line.front() == '#')
            continue;
        const auto eq = line.find('=');
        if(eq == std::string::npos)
            continue;
        reloaded_env.set(line.substr(0, eq), line.substr(eq + 1));
    }

    auto second = load(space, source_stack{std::move(reloaded_env)}, {});
    REQUIRE(second);
    const configuration &c2 = second.value();

    // Scalar values survive the env round-trip.
    REQUIRE(c2.get("host") == c1.get("host"));
    REQUIRE(c2.get("port") == c1.get("port"));
    REQUIRE(c2.keys() == c1.keys());
}
