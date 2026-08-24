#include "collection_shapes.h"

#include "nucleus/config.h"
#include "../builder_result_test_support.h"

#include "nucleus/schema/schema_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
#error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

using entry_list = std::vector<std::pair<std::string, std::string>>;

nucleus::config_source_result pull_under_space(const std::string &text,
                                               const nucleus::schema_registry &reg)
{
    auto src = nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
    src.with_space_name("engine");
    src.apply_projection(reg.projection());
    return src.pull();
}

entry_list entries_of(const nucleus::config_source_batch &batch)
{
    entry_list out;
    for(const auto &emitted : batch.entries)
        out.emplace_back(emitted.path, std::string(emitted.value.text()));
    return out;
}

bool carries(const entry_list &entries, const std::string &path, const std::string &val)
{
    return std::find(entries.begin(), entries.end(), std::make_pair(path, val))
           != entries.end();
}

nucleus::schema_registry root_nodes_registry()
{
    nucleus::schema_registry reg;
    REQUIRE(reg.attach(nucleus::repeated_element("node", nucleus::anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", nucleus::anchor::keyspace("node"))));
    return reg;
}

nucleus::schema_registry root_keyed_registry()
{
    nucleus::schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(reg.attach(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))));
    REQUIRE(reg.attach(nucleus::element("port", nucleus::anchor::keyspace("server"))));
    return reg;
}

nucleus::config_space transparent_root_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_transparent_root_nodes(builder);
    return nucleus::builder_result_test::built(builder);
}

// One document carrying every root-anchored shape at once -- repeated siblings,
// keyed siblings and a plain leaf -- loaded through a named space so its root is
// transparent.
nucleus::load_result load_siblings(const nucleus::config_space &space,
                                   const std::string &selection)
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "transparent_root_siblings"));

    nucleus::load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [dir = (root / "transparent_root_siblings").string()](
                             const std::string &path) {
        auto src = nucleus::xml_source::from(nucleus::xml_source_options::of_file(
            dir + "/" + nucleus::shapes::filename_of(path)));
        src.with_space_name("engine");
        return nucleus::source_handle(std::move(src));
    };
    opts.selection = selection;
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

}

TEST_CASE("two sibling repeated elements directly under a transparent root are emitted "
          "at distinct indexed paths",
          "[collection_shapes][transparent_root][ordinal]")
{
    const nucleus::schema_registry reg = root_nodes_registry();
    const nucleus::config_source_result pulled = pull_under_space(
        "<engine><node><port>80</port></node><node><port>90</port></node></engine>", reg);
    REQUIRE(pulled);
    const entry_list entries = entries_of(pulled.value());

    REQUIRE(carries(entries, "node[0]/port", "80"));
    REQUIRE(carries(entries, "node[1]/port", "90"));
    REQUIRE_FALSE(carries(entries, "node/port", "80"));
}

TEST_CASE("two sibling primary-keyed elements directly under a transparent root carry "
          "their key value as a path segment",
          "[collection_shapes][transparent_root][keyed]")
{
    const nucleus::schema_registry reg = root_keyed_registry();
    const nucleus::config_source_result pulled = pull_under_space(
        "<engine><server name=\"alpha\"><port>1</port></server>"
        "<server name=\"beta\"><port>2</port></server></engine>", reg);
    REQUIRE(pulled);
    const entry_list entries = entries_of(pulled.value());

    REQUIRE(carries(entries, "server/alpha/port", "1"));
    REQUIRE(carries(entries, "server/beta/port", "2"));
    REQUIRE(carries(entries, "server/alpha/name", "alpha"));
    REQUIRE(carries(entries, "server/beta/name", "beta"));
}

TEST_CASE("a duplicate primary-key value on two siblings under a transparent root is "
          "rejected loudly",
          "[collection_shapes][transparent_root][keyed]")
{
    const nucleus::schema_registry reg = root_keyed_registry();
    const nucleus::config_source_result pulled = pull_under_space(
        "<engine><server name=\"alpha\"><port>1</port></server>"
        "<server name=\"alpha\"><port>2</port></server></engine>", reg);

    REQUIRE_FALSE(pulled);
    CHECK(pulled.error().code == nucleus::errc::malformed_source);
    CHECK(pulled.error().message.find("duplicate primary-key value")
          != std::string::npos);
}

TEST_CASE("two sibling repeated elements under a transparent root resolve to two "
          "instances, each keeping its own child values",
          "[collection_shapes][transparent_root][ordinal]")
{
    const nucleus::config_space space = transparent_root_space();
    const nucleus::load_result loaded = load_siblings(space, "alpha");
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));

    REQUIRE(loaded.value().get("node[0]/port") == "80");
    REQUIRE(loaded.value().get("node[0]/label") == "first");
    REQUIRE(loaded.value().get("node[1]/port") == "90");
    REQUIRE(loaded.value().get("node[1]/label") == "second");
}

TEST_CASE("a root-anchored leaf child of a transparent root still yields its own text",
          "[collection_shapes][transparent_root][leaf]")
{
    const nucleus::config_space space = transparent_root_space();
    const nucleus::load_result loaded = load_siblings(space, "alpha");
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));

    REQUIRE(loaded.value().get("motd") == "hello");
}

TEST_CASE("two sibling primary-keyed elements under a transparent root reach strain "
          "selection rather than collapsing onto one path",
          "[collection_shapes][transparent_root][keyed]")
{
    const nucleus::config_space space = transparent_root_space();

    const nucleus::load_result alpha = load_siblings(space, "alpha");
    REQUIRE(alpha);
    INFO(nucleus::shapes::serialize(alpha.value()));
    REQUIRE(alpha.value().get("server/port") == "1");

    const nucleus::load_result beta = load_siblings(space, "beta");
    REQUIRE(beta);
    INFO(nucleus::shapes::serialize(beta.value()));
    REQUIRE(beta.value().get("server/port") == "2");
}
