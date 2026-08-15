#ifndef HPP_GUARD_NUCLEUS_TESTS_IDENTITY_ENVELOPE_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_IDENTITY_ENVELOPE_TEST_SUPPORT_H

#include "nucleus/config_space.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <string_view>

namespace nucleus::identity_envelope_test {

inline source_handle xml_of(std::string text, std::string_view space_name = {})
{
    xml_source source = xml_source::from(
            xml_source_options::of_string(std::move(text)));
    if(!space_name.empty())
        source.with_space_name(space_name);
    return source_handle(std::move(source));
}

inline load_options make_options(std::string      document,
                                 std::string_view space_name = {})
{
    load_options options;
    options.document_paths = {"doc.xml"};
    options.make_document  = [text = std::move(document),
                              name = std::string(space_name)](const std::string &)
    {
        return xml_of(text, name);
    };
    return options;
}

inline config_space typed_plugin_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("plugin", anchor::root())));
    REQUIRE(builder.register_element(
            element("x", anchor::keyspace("plugin"))));
    return builder.build();
}

inline config_space plugin_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_schema("plugin/x"));
    return builder.build();
}

}

#endif
