#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

// Selection-path tests: the facade select() method designates a single named
// strain by primary-key value; resolve() prunes non-matching strains and strips
// the transient key segment. Unknown selections and missing primary keys both
// fail loudly. The shapes here are generic (a cluster of servers) -- no host
// vocabulary.

using nucleus::anchor;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from_string(text));
}

// A schema where <cluster> contains repeatable <server> instances keyed by the
// `name` field; each server carries non-key `port` / `protocol` leaves.
void declare_cluster(nucleus::configuration_space &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server")));
}

}

TEST_CASE("select() resolves the matching strain and prunes others",
          "[selection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    REQUIRE(engine.select("web"));

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Only the web strain survives; its entries live at the declared paths.
    REQUIRE(config.get("cluster/server/port") == "80");

    // The key field is consumed -- not a leaf.
    REQUIRE_FALSE(config.contains("cluster/server/name"));
    // Transient key segments do not appear as path segments.
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
    REQUIRE_FALSE(config.contains("cluster/server/db/port"));
}

TEST_CASE("select() strips the key segment from the resolved path",
          "[selection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    REQUIRE(engine.select("web"));

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "80");
    // The transient key segment must be absent from the resolved tree.
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}

TEST_CASE("selecting an unknown strain value fails with available listed",
          "[selection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    REQUIRE(engine.select("missing"));

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);

    // The error must name the requested value and list every available strain.
    REQUIRE(loaded.error().find("missing") != std::string::npos);
    REQUIRE(loaded.error().find("'web'") != std::string::npos);
    REQUIRE(loaded.error().find("'db'") != std::string::npos);
}

TEST_CASE("selecting when schema has no primary key fails",
          "[selection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server><port>80</port></server>
        </cluster>)";

    // Schema with only plain elements -- no identity / primary_key_element.
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));

    REQUIRE(engine.select("anything"));

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("no primary key") != std::string::npos);
}

TEST_CASE("select() after resolve() is rejected",
          "[selection][facade]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    // First resolve succeeds without a selection (single strain auto-resolves).
    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE(loaded);

    // After resolve the facade is sealed; select() is a state-machine error,
    // and the diagnostic names the operation that was attempted.
    auto result = engine.select("web");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().find("select") != std::string::npos);
    REQUIRE(result.error().find("resolved") != std::string::npos);
}

TEST_CASE("anonymous-only content collapses without a selection",
          "[selection][keyed]")
{
    // No named strain anywhere -- the composed template is the configuration.
    // No select() call: the anonymous-only path should resolve without error.
    const char *doc = R"(
        <cluster>
            <server><port>8080</port><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "8080");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
}

TEST_CASE("select() against anonymous-only content fails loudly",
          "[selection][keyed]")
{
    // The container holds only an anonymous template instance: a selection is
    // unsatisfiable and must be rejected, never silently dropped in favor of
    // whatever template content exists.
    const char *doc = R"(
        <cluster>
            <server><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    REQUIRE(engine.select("web"));

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("web") != std::string::npos);
    REQUIRE(loaded.error().find("no primary-keyed instances") != std::string::npos);
}

TEST_CASE("a key value shadowing a declared element name is a loud error",
          "[selection][keyed]")
{
    // A strain literally named after a sibling element ("port") can never be
    // bucketed -- its projected path collides with the declared leaf. The
    // resolve names the collision instead of failing later with an unrelated
    // unknown-key suggestion.
    const char *doc = R"(
        <cluster>
            <server name="port"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [doc](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("primary-key value 'port'") != std::string::npos);
    REQUIRE(loaded.error().find("collides") != std::string::npos);
}
