#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <optional>

// Instance-distinguished projection and the unified-hierarchy contract: when the
// schema declares the config space's primary key, a document source keeps
// repeated container instances distinct through resolution (no last-wins
// collapse), and the resolve boundary collapses the surviving strain onto the
// declared paths. A key value NEVER appears in a resolved config -- not
// as a leaf and not as a path segment -- so the resolved tree is traversable
// without knowing the key. Several named strains with no selection fail loudly.
// The shapes here are generic (a cluster of servers) -- no host vocabulary.

using nucleus::anchor;

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

// A schema where <cluster> contains repeatable <server> instances keyed by the
// `name` field; each server carries non-key `port` / `protocol` leaves.
void declare_cluster(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server"))));
}


// Loads `doc` as the sole document layer against `space`, with an optional strain
// selection.
nucleus::load_result load_doc(const nucleus::config_space &space, const char *doc,
                              std::optional<std::string> selection = std::nullopt)
{
    nucleus::load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [doc](const std::string &) { return xml_of(doc); };
    opts.selection = std::move(selection);
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

}

TEST_CASE("a single named strain resolves onto the unified hierarchy",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // The strain's entries live at the declared paths: the key value names
    // nothing in the resolved tree, so traversal needs no key knowledge.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));

    // The key field is retained as a readable leaf, not consumed.
    REQUIRE(config.get("cluster/server/name") == "web");
}

TEST_CASE("several named strains with no selection fail loudly",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_doc(space, doc);

    // Resolving "whichever strain" is undefined and must never happen silently:
    // the error names the container and every strain it found.
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("primary-keyed instances") != std::string::npos);
    REQUIRE(loaded.error().message.find("'web'") != std::string::npos);
    REQUIRE(loaded.error().message.find("'db'") != std::string::npos);
}

TEST_CASE("a key carried as a text-leaf child is consumed the same way",
          "[projection][keyed]")
{
    const char *doc = R"(
        <cluster>
            <server><name>web</name><port>80</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "80");
    // The text-leaf-form pkey is also retained as a readable leaf.
    REQUIRE(config.get("cluster/server/name") == "web");
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // Named overrides template; template fields the strain leaves alone survive.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}

TEST_CASE("anonymous strains alone collapse into the config space",
          "[projection][keyed]")
{
    // No named strain anywhere: the composed template IS the config.
    // Later template parts override earlier ones in document order.
    const char *doc = R"(
        <cluster>
            <server><port>1</port></server>
            <server><port>8080</port><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "8080");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
    REQUIRE_FALSE(config.contains("cluster/server/name"));
}

TEST_CASE("an explicitly-empty primary-key value is rejected",
          "[projection][keyed]")
{
    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // A present-but-empty key is an empty identity, not an anonymous instance:
    // it cannot silently degrade to the structural walk and shadow real strains.
    SECTION("empty key attribute")
    {
        const char *doc = R"(
            <cluster>
                <server name=""><port>80</port></server>
            </cluster>)";
        auto loaded = load_doc(space, doc);
        REQUIRE_FALSE(loaded);
        REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
        REQUIRE(loaded.error().message.find("empty primary-key value")
                != std::string::npos);
        REQUIRE(loaded.error().message.find("cluster/server") != std::string::npos);
    }

    SECTION("empty key text child")
    {
        const char *doc = R"(
            <cluster>
                <server><name></name><port>80</port></server>
            </cluster>)";
        auto loaded = load_doc(space, doc);
        REQUIRE_FALSE(loaded);
        REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
        REQUIRE(loaded.error().message.find("empty primary-key value")
                != std::string::npos);
    }
}

TEST_CASE("a genuinely absent key still loads as an anonymous instance",
          "[projection][keyed]")
{
    // The companion to the empty-key rejection: absence (no key attribute and no
    // key child) is unchanged -- the instance is an anonymous template.
    const char *doc = R"(
        <cluster>
            <server><port>80</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/name"));
}

// The keyed-container projection lookup reads the RAW walk path. The two shapes
// that could put a stray key value or ordinal on that path -- a keyed container
// nested under another keyed container, or under a repeated container -- are both
// rejected by the schema at attach (a config space has exactly one primary key,
// and a primary key may not sit under a repeated ancestor). These cases lock that
// invariant: a keyed container's path can never carry an enclosing key-value or
// ordinal segment, so the lookup needs no canonicalization. (Canonicalizing the
// lookup with declared_path here would be unsound: under an anonymous instance the
// walk emits no key-value segment, so declared_path over-strips a genuine nested
// child -- see the anonymous-nested-container case below.)
TEST_CASE("a config space admits only one primary key (no keyed-under-keyed)",
          "[projection][keyed][schema-invariant]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("route", anchor::keyspace("cluster/server"))));

    auto second = engine.register_element(
        nucleus::primary_key_element("id", anchor::keyspace("cluster/server/route")));
    REQUIRE_FALSE(second);
    REQUIRE(second.error().message.find("exactly one") != std::string::npos);
}

TEST_CASE("a primary key may not sit under a repeated ancestor (no keyed-under-repeated)",
          "[projection][keyed][schema-invariant]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("root", anchor::root())));
    REQUIRE(engine.register_element(nucleus::repeated_element("rack", anchor::keyspace("root"))));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("root/rack"))));

    auto keyed = engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("root/rack/server")));
    REQUIRE_FALSE(keyed);
    REQUIRE(keyed.error().message.find("repeated ancestor") != std::string::npos);
}

TEST_CASE("an anonymous keyed instance keeps its nested containers intact",
          "[projection][keyed]")
{
    // An anonymous <server> (no key) descends without a key-value path segment,
    // so a nested container under it sits at cluster/server/<child>. The keyed
    // lookup must read that path as-is. A declared_path canonicalization would
    // strip <child> as if it were the (absent) server key value, collapse
    // cluster/server/profile -> cluster/server (whose key is 'name'), and then --
    // because the nested <profile> carries its own name="demo" -- mistake the
    // child for a keyed instance, inserting a spurious 'demo' path segment. Assert
    // the nested container's contents survive under the declared path.
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("profile", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::unique_element("name", anchor::keyspace("cluster/server/profile"))));
    REQUIRE(engine.register_element(
        nucleus::element("message", anchor::keyspace("cluster/server/profile"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const char *doc = R"(
        <cluster>
            <server>
                <profile name="demo"><message>hi</message></profile>
            </server>
        </cluster>)";

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/profile/message") == "hi");
    REQUIRE(config.get("cluster/server/profile/name") == "demo");
    REQUIRE_FALSE(config.contains("cluster/server/profile/demo/message"));
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
    nucleus::config_space space = nucleus::builder_result_test::built(nucleus::config_space_builder{});
    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/name") == "web");
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}
