#include "nucleus/config.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/env/env_source.h"
#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <optional>

// System-level extension of the buffer-drop invariant to the new explicit-stack
// path with a richer stack (XML document with inheritance chain + env overlay).
//
// The config is moved out of an inner scope. The entire source_stack, the
// space, and the XML arena are then destroyed. Every value is read back afterward.
// Under AddressSanitizer this proves the config is self-owning on the new
// path: the copy-out at the load boundary severed every view from its source.

using namespace nucleus;

namespace {

std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

source_handle xml_of(const std::string &text)
{
    return source_handle(
        xml_source::from(xml_source_options::of_string(text)));
}

// Two-file inheritance chain: base defines host and mode; derived extends and
// overrides mode, adds port.
constexpr const char *kBase =
    "<server>"
    "<host>base-host</host>"
    "<mode>secondary</mode>"
    "</server>";

constexpr const char *kDerived =
    "<server inherit=\"base.xml\">"
    "<mode>primary</mode>"
    "<port>9000</port>"
    "</server>";

config_space chain_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("server", anchor::root())));
    REQUIRE(builder.register_element(element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("mode", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("port", anchor::keyspace("server"))));
    return builder.build();
}

source_handle chain_document(const std::string &path)
{
    const std::string name = filename_of(path);
    if(name == "base.xml")    return xml_of(kBase);
    if(name == "derived.xml") return xml_of(kDerived);
    return source_handle(env_source{});
}

// Returns with the space, the source_stack, the env overlay and the pugixml arena the XML
// source viewed into all destroyed. Anything the config still borrowed from them dangles
// from here on, which is what the reads back in the caller prove it does not.
load_result load_over_dropped_sources()
{
    config_space space = chain_space();

    // The document chain forms the base; env is a stack source that ranks ABOVE the
    // whole chain and overrides it where the two contest a key.
    env_source overlay;
    overlay.set("server/port", "8888");

    load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = chain_document;

    return load_config(space, source_stack{std::move(overlay)}, opts);
}

}

TEST_CASE("config outlives dropped source_stack, space, and XML arena on new load path",
          "[system][disconnect][lifetime]")
{
    std::optional<config> result;

    auto loaded = load_over_dropped_sources();
    REQUIRE(loaded);
    result = std::move(loaded).value();

    // The space, the stack and the arena are gone by the time the loader returned.

    REQUIRE(result.has_value());

    // Read EVERY value back after all sources are dropped.
    // Under ASan this is the proof the config is fully self-owning.
    REQUIRE(result->get("server/host") == "base-host");
    REQUIRE(result->get("server/mode") == "primary");
    // The env overlay is a stack source ranked above the document base, so it
    // wins the port contest: env set 8888, the document set 9000, env overrides.
    REQUIRE(result->get("server/port") == "8888");

    // Provenance also survived the drop.
    const origin *host_origin = result->provenance_of("server/host");
    REQUIRE(host_origin != nullptr);

    const origin *mode_origin = result->provenance_of("server/mode");
    REQUIRE(mode_origin != nullptr);

    const origin *port_origin = result->provenance_of("server/port");
    REQUIRE(port_origin != nullptr);
}

TEST_CASE("config outlives a drop of the simplest xml+env stack on the new path",
          "[system][disconnect][lifetime]")
{
    // Minimal shape: one XML document source + one env overlay, no inheritance chain.
    std::optional<config> result;

    {
        config_space space = nucleus::builder_result_test::built(config_space_builder{});

        constexpr const char *kDoc =
            "<app>"
            "<logging level=\"debug\" file=\"/var/log/app.log\"/>"
            "<server host=\"127.0.0.1\" port=\"8080\"/>"
            "</app>";

        // Document source at stack[0]; env overlay at stack[1] overrides port.
        auto doc = xml_source::from(xml_source_options::of_string(kDoc));
        env_source overlay;
        overlay.set("app/server/port", "9090");

        auto loaded = load_config(space,
                           source_stack{std::move(doc), std::move(overlay)},
                           {});
        REQUIRE(loaded);
        result = std::move(loaded).value();
        // Every source is destroyed here.
    }

    REQUIRE(result.has_value());

    // All values readable after the entire source stack is gone.
    REQUIRE(result->get("app/logging/level") == "debug");
    REQUIRE(result->get("app/logging/file") == "/var/log/app.log");
    REQUIRE(result->get("app/server/host") == "127.0.0.1");
    REQUIRE(result->get("app/server/port") == "9090");  // overlay won

    const origin *port_origin = result->provenance_of("app/server/port");
    REQUIRE(port_origin != nullptr);
    REQUIRE(port_origin->layer == "stack[1]");

    const origin *host_origin = result->provenance_of("app/server/host");
    REQUIRE(host_origin != nullptr);
    REQUIRE(host_origin->layer == "stack[0]");
}
