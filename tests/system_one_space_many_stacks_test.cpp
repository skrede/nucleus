#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/env/env_source.h"
#include "nucleus/xml/xml_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Trident reusable-authority property: one config_space serves two DIFFERENT
// source stacks and yields two INDEPENDENT configurations. The space does not hold
// mutable state -- it is borrowed by const reference during each load. Loading a
// second stack against the same space must not perturb the first result, and both
// configurations must be simultaneously readable.

using namespace nucleus;

namespace {

source_handle xml_of(const std::string &text)
{
    return source_handle(
        xml_source::from(xml_source_options::of_string(text)));
}

// Schema: server with host, mode, and port.
config_space make_server_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("server", anchor::root())));
    REQUIRE(builder.register_element(element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(
        enum_element("mode", anchor::keyspace("server"),
                     std::vector<std::string>{"primary", "secondary"})));
    REQUIRE(builder.register_element(element("port", anchor::keyspace("server"))));
    return nucleus::builder_result_test::built(builder);
}

}

TEST_CASE("one space, two different stacks yield two independent configurations",
          "[system][one_space_many_stacks]")
{
    // One sealed space -- shared across both loads by const reference.
    const config_space space = make_server_space();

    // Stack A: primary profile.
    runtime_source primary_src;
    primary_src.set("server/host", "primary-host")
               .set("server/mode", "primary")
               .set("server/port", "8000");

    // Stack B: secondary profile.
    runtime_source secondary_src;
    secondary_src.set("server/host", "secondary-host")
                 .set("server/mode", "secondary")
                 .set("server/port", "9000");

    auto loaded_a = load_config(space, source_stack{std::move(primary_src)}, {});
    REQUIRE(loaded_a);
    const config config_a = std::move(loaded_a).value();

    auto loaded_b = load_config(space, source_stack{std::move(secondary_src)}, {});
    REQUIRE(loaded_b);
    const config config_b = std::move(loaded_b).value();

    // Both simultaneously valid: A and B can be read in any order.
    REQUIRE(config_a.get("server/host") == "primary-host");
    REQUIRE(config_a.get("server/mode") == "primary");
    REQUIRE(config_a.get("server/port") == "8000");

    REQUIRE(config_b.get("server/host") == "secondary-host");
    REQUIRE(config_b.get("server/mode") == "secondary");
    REQUIRE(config_b.get("server/port") == "9000");

    // Values differ where the stacks differ.
    REQUIRE(config_a.get("server/host") != config_b.get("server/host"));
    REQUIRE(config_a.get("server/mode") != config_b.get("server/mode"));
    REQUIRE(config_a.get("server/port") != config_b.get("server/port"));

    // Key sets are the same (same space, different values).
    REQUIRE(config_a.keys() == config_b.keys());
}

TEST_CASE("same space loaded with an XML stack and a runtime stack yields independent configs",
          "[system][one_space_many_stacks]")
{
    const config_space space = make_server_space();

    // Stack A: an XML document source.
    constexpr const char *kDocA =
        "<server>"
        "<host>xml-host</host>"
        "<mode>primary</mode>"
        "<port>7070</port>"
        "</server>";

    load_options opts_a;
    opts_a.document_paths = {"a.xml"};
    opts_a.make_document  = [](const std::string &) -> source_handle { return xml_of(kDocA); };

    auto loaded_a = load_config(space, source_stack{}, opts_a);
    REQUIRE(loaded_a);
    const config config_a = std::move(loaded_a).value();

    // Stack B: a runtime_source with different values.
    runtime_source rt;
    rt.set("server/host", "rt-host")
      .set("server/mode", "secondary")
      .set("server/port", "3030");

    auto loaded_b = load_config(space, source_stack{std::move(rt)}, {});
    REQUIRE(loaded_b);
    const config config_b = std::move(loaded_b).value();

    // Both are independently readable and disagree on every value.
    REQUIRE(config_a.get("server/host") == "xml-host");
    REQUIRE(config_b.get("server/host") == "rt-host");

    REQUIRE(config_a.get("server/mode") == "primary");
    REQUIRE(config_b.get("server/mode") == "secondary");

    REQUIRE(config_a.get("server/port") == "7070");
    REQUIRE(config_b.get("server/port") == "3030");
}

TEST_CASE("loading the same space N times leaves each config independent",
          "[system][one_space_many_stacks]")
{
    const config_space space = make_server_space();

    // Three loads against the same space, each with a distinct port.
    auto make_stack = [](const char *host, const char *mode, const char *port)
    {
        runtime_source src;
        src.set("server/host", host)
           .set("server/mode", mode)
           .set("server/port", port);
        return source_stack{std::move(src)};
    };

    auto r0 = load_config(space, make_stack("h0", "primary",   "1000"), {});
    auto r1 = load_config(space, make_stack("h1", "secondary", "2000"), {});
    auto r2 = load_config(space, make_stack("h2", "primary",   "3000"), {});

    REQUIRE(r0); REQUIRE(r1); REQUIRE(r2);

    // Each result is distinct.
    REQUIRE(r0.value().get("server/port") == "1000");
    REQUIRE(r1.value().get("server/port") == "2000");
    REQUIRE(r2.value().get("server/port") == "3000");

    // All three remain simultaneously readable after all loads are done.
    REQUIRE(r0.value().get("server/host") == "h0");
    REQUIRE(r1.value().get("server/host") == "h1");
    REQUIRE(r2.value().get("server/host") == "h2");
}
