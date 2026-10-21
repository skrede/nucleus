#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/strain_scope.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <utility>

namespace {

using policy = nucleus::strain_scope_policy;
using chain  = std::map<std::string, std::string>;

struct route
{
    std::string port;
    std::string method;
    std::string origin;
};

const std::string base_origin    = "[0|path:base.xml|anonymous|0]";
const std::string derived_origin = "[1|path:derived.xml|anonymous|1]";
const std::string lower_stack    = "[0|stack[0]|anonymous|-]";
const std::string upper_stack    = "[1|stack[1]|anonymous|-]";

const std::string parent_routes = "<route><port>80</port><method>get</method></route>"
                                  "<route><port>443</port><method>post</method></route>";

constexpr policy every_policy[] = {policy::file_level, policy::space_open_container_closed,
                                   policy::container_open_until_next_strain};

nucleus::config_space scalar_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::element("motd", anchor::keyspace("cluster"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::load_result load_chain(const chain &documents, const std::string &top, policy scope)
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
    return nucleus::load_config(scalar_space(), nucleus::source_stack{}, opts);
}

chain scalar_chain(const std::string &outer, const std::string &inner)
{
    return {{"base.xml", "<cluster><motd>base-motd</motd><server name=\"primary\"><host>base-host</host>" + parent_routes + "</server></cluster>"},
            {"derived.xml", "<cluster inherit=\"base.xml\">" + outer + "<server name=\"primary\" extend=\"narrow\">" + inner + "</server></cluster>"}};
}

void require_lines(const nucleus::load_result &loaded, const std::vector<route> &routes,
                   const std::vector<std::string> &lines)
{
    REQUIRE(loaded);
    const std::string serialized = "\n" + nucleus::shapes::serialize(loaded.value());
    INFO(serialized);
    for(const std::string &line : lines)
        REQUIRE(serialized.find("\n" + line + "\n") != std::string::npos);
    for(std::size_t ordinal = 0; ordinal < routes.size(); ++ordinal)
    {
        const std::string at = "\ncluster/server/route[" + std::to_string(ordinal) + "]/";
        REQUIRE(serialized.find(at + "port = " + routes[ordinal].port + " " + routes[ordinal].origin + "\n") != std::string::npos);
        REQUIRE(serialized.find(at + "method = " + routes[ordinal].method + " " + routes[ordinal].origin + "\n") != std::string::npos);
    }
    REQUIRE_FALSE(loaded->contains("cluster/server/route[" + std::to_string(routes.size()) + "]/port"));
}

const std::vector<route> base_routes = {{"80", "get", base_origin}, {"443", "post", base_origin}};

nucleus::load_result load_stack(policy scope)
{
    const std::string       at = "cluster/server/primary/";
    nucleus::runtime_source lower;
    nucleus::runtime_source upper;
    lower.set(at + "name", "primary").set(at + "host", "base-host");
    lower.set(at + "route[0]/port", "80").set(at + "route[0]/method", "get");
    lower.set(at + "route[1]/port", "443").set(at + "route[1]/method", "post");
    upper.set(at + "host", "stack-host").set(at + "route[0]/port", "8080").set(at + "route[0]/method", "put");
    nucleus::load_options opts;
    opts.selection = "primary";
    opts.scope     = scope;
    return nucleus::load_config(scalar_space(), nucleus::source_stack{std::move(lower), std::move(upper)}, opts);
}

}

TEST_CASE("a keyed field a narrow re-open overrides keeps the parent's value where the policy excludes it",
          "[collection_shapes][keyed][restoration]")
{
    const chain       documents = scalar_chain("<motd>derived-motd</motd>", "<host>derived-host</host>");
    const std::string identity  = "cluster/server/name = primary " + derived_origin;
    require_lines(load_chain(documents, "derived.xml", policy::file_level), base_routes,
                  {identity, "cluster/server/host = base-host " + base_origin, "cluster/motd = base-motd " + base_origin});
    require_lines(load_chain(documents, "derived.xml", policy::space_open_container_closed), base_routes,
                  {identity, "cluster/server/host = base-host " + base_origin, "cluster/motd = derived-motd " + derived_origin});
    require_lines(load_chain(documents, "derived.xml", policy::container_open_until_next_strain), base_routes,
                  {identity, "cluster/server/host = derived-host " + derived_origin, "cluster/motd = derived-motd " + derived_origin});
}

TEST_CASE("a general value a derived document overrides keeps the parent's value under file_level",
          "[collection_shapes][keyed][restoration]")
{
    const chain       documents = scalar_chain("<motd>derived-motd</motd>", "");
    const std::string host      = "cluster/server/host = base-host " + base_origin;
    require_lines(load_chain(documents, "derived.xml", policy::file_level), base_routes,
                  {host, "cluster/motd = base-motd " + base_origin});
    require_lines(load_chain(documents, "derived.xml", policy::space_open_container_closed), base_routes,
                  {host, "cluster/motd = derived-motd " + derived_origin});
    require_lines(load_chain(documents, "derived.xml", policy::container_open_until_next_strain), base_routes,
                  {host, "cluster/motd = derived-motd " + derived_origin});
}

TEST_CASE("a stack source the policy excludes leaves the lower source's keyed values in place",
          "[collection_shapes][keyed][restoration]")
{
    const std::string        identity = "cluster/server/name = primary " + lower_stack;
    const std::vector<route> lower    = {{"80", "get", lower_stack}, {"443", "post", lower_stack}};
    require_lines(load_stack(policy::file_level), lower, {identity, "cluster/server/host = base-host " + lower_stack});
    require_lines(load_stack(policy::space_open_container_closed), lower,
                  {identity, "cluster/server/host = base-host " + lower_stack});
    require_lines(load_stack(policy::container_open_until_next_strain),
                  {{"8080", "put", upper_stack}, {"443", "post", lower_stack}},
                  {identity, "cluster/server/host = stack-host " + upper_stack});
}

TEST_CASE("a narrow re-open above a competing strain leaves the parent's routes in place",
          "[collection_shapes][keyed][restoration]")
{
    const chain documents = {
            {"base.xml", "<cluster><server name=\"primary\">" + parent_routes + "</server></cluster>"},
            {"mid.xml", "<cluster inherit=\"base.xml\"><server name=\"secondary\"><route><port>9000</port><method>head</method></route></server></cluster>"},
            {"top.xml", "<cluster inherit=\"mid.xml\"><server name=\"primary\" extend=\"narrow\"><route><port>8080</port><method>put</method></route></server></cluster>"}};
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_lines(load_chain(documents, "top.xml", scope), base_routes,
                      {"cluster/server/name = primary [2|path:top.xml|anonymous|2]"});
    }
}
