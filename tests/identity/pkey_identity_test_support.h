#ifndef HPP_GUARD_NUCLEUS_TESTS_IDENTITY_PKEY_IDENTITY_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_IDENTITY_PKEY_IDENTITY_TEST_SUPPORT_H

#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <optional>

namespace nucleus::pkey_test {

inline source_handle xml_of(std::string text)
{
    return source_handle(
            xml_source::from(xml_source_options::of_string(std::move(text))));
}

inline config_space cluster_space(bool required_key = false)
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            element("server", anchor::keyspace("cluster"))));
    schema_element key = primary_key_element(
            "name", anchor::keyspace("cluster/server"));
    key.required = required_key;
    REQUIRE(builder.register_element(std::move(key)));
    REQUIRE(builder.register_element(
            element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
            element("protocol", anchor::keyspace("cluster/server"))));
    return nucleus::builder_result_test::built(builder);
}

inline load_result load_doc(const config_space &space, std::string document,
                            std::optional<std::string> selection = std::nullopt)
{
    load_options options;
    options.document_paths = {"doc.xml"};
    options.make_document  = [text = std::move(document)](const std::string &)
    {
        return xml_of(text);
    };
    options.selection = std::move(selection);
    return load_config(space, source_stack{}, options);
}

}

#endif
