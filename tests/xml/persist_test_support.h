#ifndef HPP_GUARD_NUCLEUS_TESTS_XML_PERSIST_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_XML_PERSIST_TEST_SUPPORT_H

#include "xml/persist_artifact.h"

#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <utility>

namespace nucleus::xml_persist_test {

// A failed lifecycle step carries the operation and the system reason; surface that
// in the assertion rather than letting a bare false say only that something broke.
inline void check_step(const expected<void, std::string> &step)
{
    INFO((step ? std::string() : step.error()));
    REQUIRE(step);
}

template<typename T>
T checked(const expected<T, std::string> &step)
{
    INFO((step ? std::string() : step.error()));
    REQUIRE(step);
    return step.value();
}

// Every transition of a checked write, so a case whose subject is elsewhere states
// the write once instead of restating the lifecycle.
inline void write_text(temporary_artifact &artifact, std::string_view text)
{
    check_step(artifact.open_out());
    artifact.out() << text;
    check_step(artifact.flush_and_close());
}

inline constexpr char server_document[] =
        "<server>\n"
        "  <host>localhost</host>\n"
        "  <tag>alpha</tag>\n"
        "  <tag>beta</tag>\n"
        "</server>\n";

inline config checked_config(std::map<std::string, std::string> values)
{
    auto made = config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

inline xml_source xml_of(std::string text)
{
    return xml_source::from(xml_source_options::of_string(std::move(text)));
}

inline config_space server_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("server", anchor::root())));
    REQUIRE(builder.register_element(
            element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(
            repeated_element("tag", anchor::keyspace("server"))));
    return nucleus::builder_result_test::built(builder);
}

inline config_space port_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("port", anchor::root())));
    return nucleus::builder_result_test::built(builder);
}

inline config_space repeated_tags_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            repeated_element("tags", anchor::keyspace("cluster"))));
    return nucleus::builder_result_test::built(builder);
}

inline source_handle source_of(std::string text)
{
    return source_handle(xml_of(std::move(text)));
}

inline load_options document_options(std::string document)
{
    load_options options;
    options.document_paths = {"doc.xml"};
    options.make_document  = [text = std::move(document)](const std::string &)
    {
        return source_of(text);
    };
    return options;
}

}

#endif
