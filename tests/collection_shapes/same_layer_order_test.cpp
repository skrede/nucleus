#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/strain_scope.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <optional>

namespace {

using policy = nucleus::strain_scope_policy;
using values = std::vector<std::pair<std::string, std::string>>;

constexpr policy every_policy[] = {policy::file_level, policy::space_open_container_closed,
                                   policy::container_open_until_next_strain};

nucleus::config_space cluster_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_cluster_nodes_routes(builder);
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space keyed_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("node", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::element("label", anchor::keyspace("cluster/server/node"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("route", anchor::keyspace("cluster/server/node"))));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("cluster/server/node/route"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::source_stack runtime_stack(values entries)
{
    return nucleus::source_stack{nucleus::shapes::runtime_layer(std::move(entries))};
}

nucleus::load_result load_one(const nucleus::config_space &space, const std::string &document, policy scope,
                              nucleus::source_stack      stack     = nucleus::source_stack{},
                              std::optional<std::string> selection = {})
{
    nucleus::load_options opts;
    if(!document.empty())
        opts.document_paths = {"document.xml"};
    opts.make_document = [document](const std::string &)
    {
        return nucleus::source_handle(
                nucleus::xml_source::from(nucleus::xml_source_options::of_string(document)));
    };
    opts.selection = std::move(selection);
    opts.scope     = scope;
    return nucleus::load_config(space, stack, opts);
}

void require_values(const nucleus::load_result &loaded, const values &expected,
                    const std::vector<std::string> &absent = {})
{
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));
    for(const auto &[key, value] : expected)
    {
        CAPTURE(key);
        REQUIRE(loaded->get(key) == value);
    }
    for(const std::string &key : absent)
        REQUIRE_FALSE(loaded->contains(key));
}

void require_document(const std::string &document, const values &expected)
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_values(load_one(cluster_space(), document, scope), expected);
    }
}
}

TEST_CASE("a layer keeps a nested instance it wrote before a field of the same instance",
          "[collection_shapes][same_layer]")
{
    require_document("<cluster><node><route><port>300</port></route><label>L2</label></node></cluster>",
                     {{"cluster/node[0]/label", "L2"}, {"cluster/node[0]/route[0]/port", "300"}});
}

TEST_CASE("a layer keeps a nested instance it wrote after a field of the same instance",
          "[collection_shapes][same_layer]")
{
    require_document("<cluster><node><label>L2</label><route><port>300</port></route></node></cluster>",
                     {{"cluster/node[0]/label", "L2"}, {"cluster/node[0]/route[0]/port", "300"}});
}

TEST_CASE("a layer keeps the repeated leaves it wrote before a field of the same instance",
          "[collection_shapes][same_layer]")
{
    require_document("<cluster><node><mark>a</mark><mark>b</mark><label>L2</label></node></cluster>",
                     {{"cluster/node[0]/mark[0]", "a"}, {"cluster/node[0]/mark[1]", "b"}, {"cluster/node[0]/label", "L2"}});
    require_document("<cluster><node><mark>m</mark><label>L2</label></node></cluster>",
                     {{"cluster/node[0]/mark[0]", "m"}, {"cluster/node[0]/label", "L2"}});
}

TEST_CASE("a runtime layer keeps a nested instance it wrote before a field of the same instance",
          "[collection_shapes][same_layer]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        const values entries{{"cluster/node[0]/route[0]/port", "1"}, {"cluster/node[0]/label", "x"}};
        require_values(load_one(cluster_space(), {}, scope, runtime_stack(entries)), entries);
    }
}

TEST_CASE("a selected strain keeps a nested instance it wrote before a field of the same instance",
          "[collection_shapes][same_layer]")
{
    const std::string document = "<cluster><server name=\"primary\"><node><route><port>300</port></route>"
                                 "<label>L2</label></node></server></cluster>";
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_values(load_one(keyed_space(), document, scope, nucleus::source_stack{}, "primary"),
                       {{"cluster/server/node[0]/route[0]/port", "300"}, {"cluster/server/node[0]/label", "L2"}});
    }
}

TEST_CASE("a layer keeps a repeated collection it wrote before a sibling field",
          "[collection_shapes][same_layer]")
{
    require_document("<cluster><node><tags><name>t0</name></tags><tags><name>t1</name></tags>"
                     "<label>L2</label></node></cluster>",
                     {{"cluster/node[0]/tags[0]/name", "t0"}, {"cluster/node[0]/tags[1]/name", "t1"}, {"cluster/node[0]/label", "L2"}});
}

TEST_CASE("a higher layer still replaces the whole instance its outer field names",
          "[collection_shapes][same_layer]")
{
    const std::string base = "<cluster><node><route><port>1</port></route><route><port>2</port></route>"
                             "<label>L0</label></node></cluster>";
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        const values top{{"cluster/node[0]/route[0]/port", "9"}, {"cluster/node[0]/label", "L9"}};
        require_values(load_one(cluster_space(), base, scope, runtime_stack(top)), top,
                       {"cluster/node[0]/route[1]/port"});
    }
}

TEST_CASE("a higher layer writing only a nested instance keeps the base's outer field",
          "[collection_shapes][same_layer]")
{
    const std::string base = "<cluster><node><label>L0</label><route><port>1</port></route></node></cluster>";
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_values(load_one(cluster_space(), base, scope,
                                runtime_stack({{"cluster/node[0]/route[0]/port", "9"}})),
                       {{"cluster/node[0]/label", "L0"}, {"cluster/node[0]/route[0]/port", "9"}});
    }
}
