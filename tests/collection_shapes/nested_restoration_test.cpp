#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/strain_scope.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

namespace {

using policy = nucleus::strain_scope_policy;
using chain  = std::map<std::string, std::string>;

const std::string base_origin = "[0|path:base.xml|anonymous|0]";
const std::string mid_origin  = "[1|path:mid.xml|anonymous|1]";
const std::string top_origin  = "[2|path:top.xml|anonymous|2]";

constexpr policy every_policy[] = {policy::file_level, policy::space_open_container_closed,
                                   policy::container_open_until_next_strain};

nucleus::config_space nested_space()
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
    REQUIRE(builder.register_element(nucleus::element("method", anchor::keyspace("cluster/server/node/route"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("tag", anchor::keyspace("cluster/server/node/route"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::load_result load_chain(const nucleus::config_space &space, const chain &documents, const std::string &top,
                                policy scope, nucleus::source_stack stack = nucleus::source_stack{})
{
    nucleus::load_options opts;
    opts.document_paths = {top};
    opts.make_document  = [documents](const std::string &path)
    {
        return nucleus::source_handle(nucleus::xml_source::from(nucleus::xml_source_options::of_string(
                documents.at(nucleus::shapes::filename_of(path)))));
    };
    opts.selection = "primary";
    opts.scope     = scope;
    return nucleus::load_config(space, stack, opts);
}

std::string reopen(const std::string &parent, const std::string &extend, const std::string &inner)
{
    return "<cluster inherit=\"" + parent + "\"><server name=\"primary\" extend=\"" + extend + "\">" + inner + "</server></cluster>";
}

void require_lines(const nucleus::load_result &loaded, const std::vector<std::string> &lines,
                   const std::vector<std::string> &absent)
{
    REQUIRE(loaded);
    const std::string serialized = "\n" + nucleus::shapes::serialize(loaded.value());
    INFO(serialized);
    for(const std::string &line : lines)
        REQUIRE(serialized.find("\n" + line + "\n") != std::string::npos);
    for(const std::string &key : absent)
        REQUIRE_FALSE(loaded->contains(key));
}

chain nested_chain(const std::string &top_inner)
{
    return {{"base.xml", "<cluster><server name=\"primary\"><node><label>L0</label>"
                         "<route><port>100</port><method>a0</method></route></node></server></cluster>"},
            {"mid.xml", reopen("base.xml", "narrow", "<node><route><port>900</port></route></node>")},
            {"top.xml", reopen("mid.xml", "wide", top_inner)}};
}

chain emptied_chain(const std::string &top_route)
{
    return {{"base.xml", "<cluster><server name=\"primary\"><node><label>L0</label><route><port>100</port>"
                         "<method>a0</method><tag>t0</tag><tag>t1</tag></route></node></server></cluster>"},
            {"mid.xml", reopen("base.xml", "narrow", "<node><label>M</label></node>")},
            {"top.xml", reopen("mid.xml", "wide", "<node><route>" + top_route + "</route></node>")}};
}

std::string emptied_label(policy scope)
{
    if(scope == policy::container_open_until_next_strain)
        return "cluster/server/node[0]/label = M " + mid_origin;
    return "cluster/server/node[0]/label = L0 " + base_origin;
}
}

TEST_CASE("a wide re-open keeps the base's nested instance out of the instance it replaced",
          "[collection_shapes][keyed][restoration][nested]")
{
    const chain documents = nested_chain("<node><label>L2</label></node>");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_lines(load_chain(nested_space(), documents, "top.xml", scope),
                      {"cluster/server/name = primary " + top_origin, "cluster/server/node[0]/label = L2 " + top_origin},
                      {"cluster/server/node[0]/route[0]/port", "cluster/server/node[0]/route[0]/method"});
    }
}

TEST_CASE("a wide re-open supplying one nested field keeps the base's other fields out",
          "[collection_shapes][keyed][restoration][nested]")
{
    const chain documents = nested_chain("<node><label>L2</label><route><port>300</port></route></node>");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_lines(load_chain(nested_space(), documents, "top.xml", scope),
                      {"cluster/server/node[0]/label = L2 " + top_origin,
                       "cluster/server/node[0]/route[0]/port = 300 " + top_origin},
                      {"cluster/server/node[0]/route[0]/method"});
    }
}

TEST_CASE("a wide re-open of an inner instance keeps the base's fields out after an excluded layer emptied the outer one",
          "[collection_shapes][keyed][restoration][nested]")
{
    const chain documents = emptied_chain("<port>300</port>");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_lines(load_chain(nested_space(), documents, "top.xml", scope),
                      {emptied_label(scope), "cluster/server/node[0]/route[0]/port = 300 " + top_origin},
                      {"cluster/server/node[0]/route[0]/method", "cluster/server/node[0]/route[0]/tag[0]",
                       "cluster/server/node[0]/route[0]/tag[1]"});
    }
}

TEST_CASE("a wide re-open supplying one inner field keeps the base's other fields out after an excluded layer emptied the outer one",
          "[collection_shapes][keyed][restoration][nested]")
{
    const chain documents = emptied_chain("<method>m2</method>");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_lines(load_chain(nested_space(), documents, "top.xml", scope),
                      {emptied_label(scope), "cluster/server/node[0]/route[0]/method = m2 " + top_origin},
                      {"cluster/server/node[0]/route[0]/port", "cluster/server/node[0]/route[0]/tag[0]",
                       "cluster/server/node[0]/route[0]/tag[1]"});
    }
}
