#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/group_enforcer.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/provenance.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <initializer_list>

namespace {

using nucleus::anchor;
using nucleus::schema_registry;

schema_registry output_schema()
{
    schema_registry schema;
    REQUIRE(schema.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(schema.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(schema.attach(nucleus::repeated_element("output", anchor::keyspace("cluster/node"))));
    REQUIRE(schema.attach(nucleus::element("name", anchor::keyspace("cluster/node/output"))));
    REQUIRE(schema.attach_identity_group(
            nucleus::identity_group("output_names", anchor::keyspace("cluster/node"))
                    .members({"output"})
                    .field("name")));
    return schema;
}

schema_registry flat_output_schema()
{
    schema_registry schema;
    REQUIRE(schema.attach(nucleus::element("endpoints", anchor::root())));
    REQUIRE(schema.attach(nucleus::repeated_element("output", anchor::keyspace("endpoints"))));
    REQUIRE(schema.attach(nucleus::element("name", anchor::keyspace("endpoints/output"))));
    REQUIRE(schema.attach_identity_group(
            nucleus::identity_group("output_names", anchor::keyspace("endpoints"))
                    .members({"output"})
                    .field("name")));
    return schema;
}

schema_registry nested_node_schema()
{
    schema_registry schema;
    REQUIRE(schema.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(schema.attach(nucleus::repeated_element("rack", anchor::keyspace("cluster"))));
    REQUIRE(schema.attach(nucleus::repeated_element("node", anchor::keyspace("cluster/rack"))));
    REQUIRE(schema.attach(nucleus::element("name", anchor::keyspace("cluster/rack/node"))));
    REQUIRE(schema.attach_identity_group(
            nucleus::identity_group("node_names", anchor::keyspace("cluster/rack"))
                    .members({"node"})
                    .field("name")));
    return schema;
}

nucleus::config checked_configuration(
        std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::config configuration(
        std::initializer_list<std::pair<const char *, const char *>> entries)
{
    std::map<std::string, std::string> values;
    for(const auto &[path, text] : entries)
        values.emplace(path, text);
    return checked_configuration(std::move(values));
}

bool mentions(const std::vector<nucleus::schema_violation> &violations,
              const std::string                            &needle)
{
    for(const nucleus::schema_violation &violation : violations)
        if(violation.reason.find(needle) != std::string::npos)
            return true;
    return false;
}

nucleus::config_space output_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("output", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::element("name", anchor::keyspace("cluster/node/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_names", anchor::keyspace("cluster/node"))
                    .members({"output"})
                    .field("name")));
    return std::move(builder).build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

}

TEST_CASE("identity values may repeat across enclosing instances",
          "[collection_shapes][identity]")
{
    const schema_registry schema = output_schema();
    for(std::size_t parent_count = 1; parent_count <= 5; ++parent_count)
    {
        std::map<std::string, std::string> values;
        for(std::size_t parent = 0; parent < parent_count; ++parent)
            values.emplace("cluster/node[" + std::to_string(parent) + "]/output[0]/name", "shared");
        const auto violations = nucleus::group_enforcer::validate(
                schema, checked_configuration(std::move(values)));
        INFO("parent count: " << parent_count);
        if(!violations.empty())
            INFO(violations.front().reason);
        CHECK(violations.empty());
    }
}

TEST_CASE("identity values remain unique within one enclosing instance",
          "[collection_shapes][identity]")
{
    const auto violations = nucleus::group_enforcer::validate(
            output_schema(), configuration({{"cluster/node[0]/output[0]/name", "alpha"}, {"cluster/node[0]/output[1]/name", "alpha"}}));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations.front().path == "cluster/node[0]/output[0]/name");
    REQUIRE(violations.front().reason == "identity group 'output_names': identifier 'name'='alpha' is not unique within the "
                                         "slice -- declared by 'cluster/node[0]/output[0]/name' (element-type 'output'), "
                                         "'cluster/node[0]/output[1]/name' (element-type 'output')");
}

TEST_CASE("a flat identity namespace retains one value pool",
          "[collection_shapes][identity]")
{
    const auto violations = nucleus::group_enforcer::validate(
            flat_output_schema(), configuration({{"endpoints/output[0]/name", "alpha"}, {"endpoints/output[1]/name", "alpha"}}));
    REQUIRE(violations.size() == 1);
    REQUIRE(mentions(violations, "endpoints/output[0]/name"));
    REQUIRE(mentions(violations, "endpoints/output[1]/name"));
}

TEST_CASE("an identity member without its identifier keeps the existing diagnostic",
          "[collection_shapes][identity]")
{
    const auto violations = nucleus::group_enforcer::validate(
            output_schema(), configuration({{"cluster/node[0]/output[0]/port", "80"}}));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations.front().reason == "identity group 'output_names': member 'output' instance "
                                         "'cluster/node[0]/output[0]' is missing its identifier field 'name'");
}

TEST_CASE("nested identity pools reject only duplicates within one outer instance",
          "[collection_shapes][identity]")
{
    const auto violations = nucleus::group_enforcer::validate(
            nested_node_schema(), configuration({{"cluster/rack[0]/node[0]/name", "alpha"}, {"cluster/rack[0]/node[1]/name", "alpha"}, {"cluster/rack[1]/node[0]/name", "alpha"}, {"cluster/rack[1]/node[1]/name", "beta"}}));
    REQUIRE(violations.size() == 1);
    REQUIRE(mentions(violations, "cluster/rack[0]/node[0]/name"));
    REQUIRE(mentions(violations, "cluster/rack[0]/node[1]/name"));
    REQUIRE_FALSE(mentions(violations, "cluster/rack[1]/node[0]/name"));
}

TEST_CASE("one document may reuse an identity value across enclosing instances",
          "[collection_shapes][identity]")
{
    const auto loaded = nucleus::load_config(
            output_space(), nucleus::source_stack{xml_of("<cluster><node><output><name>alpha</name></output></node>"
                                                         "<node><output><name>alpha</name></output></node></cluster>")},
            {});
    const std::string diagnostic = loaded.has_value()
            ? std::string{}
            : loaded.error().message;
    INFO(diagnostic);
    REQUIRE(loaded);
    REQUIRE(loaded->get("cluster/node[1]/output[0]/name") == "alpha");
}

TEST_CASE("one document still rejects duplicate identities within an enclosing instance",
          "[collection_shapes][identity]")
{
    const auto loaded = nucleus::load_config(
            output_space(), nucleus::source_stack{xml_of("<cluster><node><output><name>alpha</name></output>"
                                                         "<output><name>alpha</name></output></node></cluster>")},
            {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("cluster/node[0]/output[0]/name") != std::string::npos);
    REQUIRE(loaded.error().message.find("cluster/node[0]/output[1]/name") != std::string::npos);
}
