#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <optional>

// Instance-distinguished projection and the unified-hierarchy contract: when the
// schema declares the configuration space's primary key, a document source keeps
// repeated container instances distinct through resolution (no last-wins
// collapse), and the resolve boundary collapses the surviving strain onto the
// declared paths. A key value NEVER appears in a resolved configuration -- not
// as a leaf and not as a path segment -- so the resolved tree is traversable
// without knowing the key. Several named strains with no selection fail loudly.
// The shapes here are generic (a cluster of servers) -- no host vocabulary.

using nucleus::anchor;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from(nucleus::xml::xml_source_options::of_string(text)));
}

// A schema where <cluster> contains repeatable <server> instances keyed by the
// `name` field; each server carries non-key `port` / `protocol` leaves.
void declare_cluster(nucleus::configuration_space_builder &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server")));
}

// Loads `doc` as the sole document layer against `space`, with an optional strain
// selection -- the per-load shape replacing the old paths-and-factory load.
nucleus::load_result load_doc(const nucleus::configuration_space &space, const char *doc,
                              std::optional<std::string> selection = std::nullopt)
{
    nucleus::source_stack_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [doc](const std::string &) { return xml_of(doc); };
    opts.selection = std::move(selection);
    return nucleus::load_configuration(space, opts);
}

}

TEST_CASE("a single named strain resolves onto the unified hierarchy",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space_builder engine;
    declare_cluster(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // The strain's entries live at the declared paths: the key value names
    // nothing in the resolved tree, so traversal needs no key knowledge.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));

    // The key field is consumed outright -- not a leaf either.
    REQUIRE_FALSE(config.contains("cluster/server/name"));
}

TEST_CASE("several named strains with no selection fail loudly",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    nucleus::configuration_space_builder engine;
    declare_cluster(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = load_doc(space, doc);

    // Resolving "whichever strain" is undefined and must never happen silently:
    // the error names the container and every strain it found.
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("primary-keyed instances") != std::string::npos);
    REQUIRE(loaded.error().find("'web'") != std::string::npos);
    REQUIRE(loaded.error().find("'db'") != std::string::npos);
}

TEST_CASE("a key carried as a text-leaf child is consumed the same way",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server><name>web</name><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space_builder engine;
    declare_cluster(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/name"));
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}

TEST_CASE("a named strain composes on top of anonymous template instances",
          "[projection][keyed]")
{
    // The keyless <server> is a template; the named strain inherits its fields
    // and overrides where both define a value.
    const char *doc = R"(
        <cluster>
            <server><port>1</port><protocol>tcp</protocol></server>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space_builder engine;
    declare_cluster(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Named overrides template; template fields the strain leaves alone survive.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}

TEST_CASE("anonymous strains alone collapse into the configuration space",
          "[projection][keyed]")
{
    // No named strain anywhere: the composed template IS the configuration.
    // Later template parts override earlier ones in document order.
    const char *doc = R"(
        <cluster>
            <server><port>1</port></server>
            <server><port>8080</port><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::configuration_space_builder engine;
    declare_cluster(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "8080");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
    REQUIRE_FALSE(config.contains("cluster/server/name"));
}

TEST_CASE("without a declared primary key the structural walk is unchanged",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    // No schema at all: projection is empty, so the source walks structurally and
    // the name attribute is an ordinary leaf.
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();
    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get("cluster/server/name") == "web");
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}
