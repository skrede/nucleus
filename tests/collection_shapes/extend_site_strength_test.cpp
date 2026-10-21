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

nucleus::config_space scalar_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("cluster/server"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::load_result load_chain(const chain &documents, const std::string &top, policy scope,
                                nucleus::source_stack stack = nucleus::source_stack{})
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
    return nucleus::load_config(scalar_space(), stack, opts);
}

std::string reopen(const std::string &parent, const std::string &extend, const std::string &inner)
{
    return "<cluster inherit=\"" + parent + "\"><server name=\"primary\" extend=\"" + extend + "\">" + inner + "</server></cluster>";
}

const std::string base_document = "<cluster><server name=\"primary\"><host>base-host</host>"
                                  "<route><port>80</port><method>get</method></route></server></cluster>";

const std::string reopened_route = "<route><port>8080</port><method>put</method></route>";

nucleus::source_stack stack_document(const std::string &extend)
{
    return nucleus::source_stack{nucleus::source_handle(nucleus::xml_source::from(nucleus::xml_source_options::of_string(
            "<cluster><server name=\"primary\" extend=\"" + extend + "\"><host>stack-host</host></server></cluster>")))};
}

std::vector<std::string> host_and_port(policy scope)
{
    if(scope == policy::container_open_until_next_strain)
        return {"cluster/server/host = stack-host [2|stack[0]|anonymous|-]",
                "cluster/server/route[0]/port = 8080 [1|path:derived.xml|anonymous|1]"};
    return {"cluster/server/host = base-host " + base_origin, "cluster/server/route[0]/port = 80 " + base_origin};
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

}

TEST_CASE("a narrow re-open above a wide one obeys the policy for its own content only",
          "[collection_shapes][keyed][extend]")
{
    const chain documents = {{"base.xml", base_document},
                             {"mid.xml", reopen("base.xml", "wide", "<host>mid-host</host>")},
                             {"top.xml", reopen("mid.xml", "narrow", reopened_route)}};
    require_lines(load_chain(documents, "top.xml", policy::space_open_container_closed),
                  {"cluster/server/name = primary " + top_origin, "cluster/server/host = mid-host " + mid_origin,
                   "cluster/server/route[0]/port = 80 " + base_origin, "cluster/server/route[0]/method = get " + base_origin},
                  {"cluster/server/route[1]/port"});
}

TEST_CASE("a wide re-open above a narrow one composes its own content only",
          "[collection_shapes][keyed][extend]")
{
    const chain documents = {{"base.xml", base_document},
                             {"mid.xml", reopen("base.xml", "narrow", "<host>mid-host</host>")},
                             {"top.xml", reopen("mid.xml", "wide", reopened_route)}};
    require_lines(load_chain(documents, "top.xml", policy::space_open_container_closed),
                  {"cluster/server/name = primary " + top_origin, "cluster/server/host = base-host " + base_origin,
                   "cluster/server/route[0]/port = 8080 " + top_origin, "cluster/server/route[0]/method = put " + top_origin},
                  {"cluster/server/route[1]/port"});
}

TEST_CASE("a stack source obeys the scope policy in a strain a document re-opens wide",
          "[collection_shapes][keyed][extend]")
{
    const chain       documents = {{"base.xml", base_document}, {"derived.xml", reopen("base.xml", "wide", reopened_route)}};
    const std::string derived   = "[1|path:derived.xml|anonymous|1]";
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        const std::string host = scope == policy::container_open_until_next_strain
                ? "stack-host [2|stack[0]|anonymous|-]"
                : "base-host " + base_origin;
        require_lines(load_chain(documents, "derived.xml", scope,
                                 nucleus::source_stack{nucleus::shapes::runtime_layer(
                                         {{"cluster/server/primary/host", "stack-host"}})}),
                      {"cluster/server/host = " + host, "cluster/server/route[0]/port = 8080 " + derived,
                       "cluster/server/route[0]/method = put " + derived},
                      {"cluster/server/route[1]/port"});
    }
}

TEST_CASE("a stack source's own extend obeys the scope policy", "[collection_shapes][keyed][extend]")
{
    const chain documents = {{"base.xml", base_document}, {"derived.xml", reopen("base.xml", "narrow", reopened_route)}};
    for(const std::string extend : {"narrow", "wide"})
        for(const policy scope : every_policy)
        {
            CAPTURE(extend, scope);
            require_lines(load_chain(documents, "derived.xml", scope, stack_document(extend)), host_and_port(scope), {});
        }
}

TEST_CASE("a stack source's extend does not answer the re-open rule for a document that declares none",
          "[collection_shapes][keyed][extend]")
{
    const chain documents = {{"base.xml", base_document},
                             {"derived.xml", "<cluster inherit=\"base.xml\"><server name=\"primary\">" + reopened_route + "</server></cluster>"}};
    for(const std::string extend : {"narrow", "wide"})
        for(const policy scope : every_policy)
        {
            CAPTURE(extend, scope);
            const nucleus::load_result loaded = load_chain(documents, "derived.xml", scope, stack_document(extend));
            REQUIRE_FALSE(loaded);
            REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
            REQUIRE(loaded.error().message.find("without an extend disposition") != std::string::npos);
        }
}

TEST_CASE("a stack source's extend over an instance only one document supplies loads",
          "[collection_shapes][keyed][extend]")
{
    for(const std::string extend : {"narrow", "wide"})
        for(const policy scope : every_policy)
        {
            CAPTURE(extend, scope);
            const std::string host = scope == policy::container_open_until_next_strain
                    ? "stack-host [1|stack[0]|anonymous|-]"
                    : "base-host " + base_origin;
            require_lines(load_chain({{"base.xml", base_document}}, "base.xml", scope, stack_document(extend)),
                          {"cluster/server/host = " + host, "cluster/server/route[0]/port = 80 " + base_origin}, {});
        }
}

TEST_CASE("a wide re-open displaces the whole instance a narrow re-open below it addressed",
          "[collection_shapes][keyed][extend]")
{
    const chain documents = {{"base.xml", base_document},
                             {"mid.xml", reopen("base.xml", "narrow", "<route><port>81</port></route>")},
                             {"top.xml", reopen("mid.xml", "wide", "<route><port>82</port></route>")}};
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_lines(load_chain(documents, "top.xml", scope),
                      {"cluster/server/host = base-host " + base_origin, "cluster/server/route[0]/port = 82 " + top_origin},
                      {"cluster/server/route[0]/method", "cluster/server/route[1]/port"});
    }
}
