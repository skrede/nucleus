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

const std::string base_origin    = "[0|path:base.xml|anonymous|0]";
const std::string derived_origin = "[1|path:derived.xml|anonymous|1]";
const std::string stack_origin   = "[2|stack[0]|anonymous|-]";

constexpr policy every_policy[] = {policy::file_level, policy::space_open_container_closed,
                                   policy::container_open_until_next_strain};

const std::string base_document = "<cluster><server name=\"primary\"><host>h0</host></server></cluster>";

nucleus::config_space host_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("cluster/server"))));
    return nucleus::builder_result_test::built(builder);
}

chain derived_over_base(const std::string &attributes, const std::string &inner)
{
    return {{"base.xml", base_document},
            {"derived.xml", "<cluster inherit=\"base.xml\"><server name=\"primary\"" + attributes + ">" + inner + "</server></cluster>"}};
}

nucleus::source_stack runtime_stack()
{
    return nucleus::source_stack{nucleus::shapes::runtime_layer(
            {{"cluster/server/primary/name", "primary"}, {"cluster/server/primary/host", "s"}})};
}

nucleus::source_stack xml_stack()
{
    return nucleus::source_stack{nucleus::source_handle(nucleus::xml_source::from(
            nucleus::xml_source_options::of_string("<cluster><server name=\"primary\"><host>s</host></server></cluster>")))};
}

std::vector<std::string> masked_lines(policy scope)
{
    const std::string host = scope == policy::container_open_until_next_strain ? "s " + stack_origin : "h0 " + base_origin;
    return {"cluster/server/host = " + host, "cluster/server/name = primary " + stack_origin};
}

void require_refused(const nucleus::load_result &loaded, const std::string &message)
{
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    REQUIRE(loaded.error().message.find(message) != std::string::npos);
}

}

TEST_CASE("an identity-only narrow re-open with no stack source loads", "[collection_shapes][keyed][reopen]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(host_space(), derived_over_base(" extend=\"narrow\"", ""), "derived.xml", scope),
                                       {"cluster/server/host = h0 " + base_origin, "cluster/server/name = primary " + derived_origin}, {});
    }
}

TEST_CASE("a stack source overwriting an identity-only narrow re-open leaves it its base",
          "[collection_shapes][keyed][reopen]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(host_space(), derived_over_base(" extend=\"narrow\"", ""), "derived.xml", scope, runtime_stack()),
                                       masked_lines(scope), {});
    }
}

TEST_CASE("a stack source overwriting every value a narrow re-open wrote leaves it its base",
          "[collection_shapes][keyed][reopen]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(host_space(), derived_over_base(" extend=\"narrow\"", "<host>h1</host>"), "derived.xml", scope, runtime_stack()),
                                       masked_lines(scope), {});
    }
}

TEST_CASE("a re-declaration without extend and no stack source is a duplicate", "[collection_shapes][keyed][reopen]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_refused(nucleus::shapes::load_chain(host_space(), derived_over_base("", "<host>h1</host>"), "derived.xml", scope),
                        "without an extend disposition");
    }
}

TEST_CASE("a stack source overwriting a derived re-declaration does not hide it from the re-open rule",
          "[collection_shapes][keyed][reopen]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        require_refused(nucleus::shapes::load_chain(host_space(), derived_over_base("", "<host>h1</host>"), "derived.xml", scope, runtime_stack()),
                        "without an extend disposition");
    }
}

TEST_CASE("a document stack source declaring no extend leaves an identity-only narrow re-open its base",
          "[collection_shapes][keyed][reopen]")
{
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(host_space(), derived_over_base(" extend=\"narrow\"", ""), "derived.xml", scope, xml_stack()),
                                       masked_lines(scope), {});
    }
}
