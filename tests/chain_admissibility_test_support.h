#ifndef HPP_GUARD_NUCLEUS_TESTS_CHAIN_ADMISSIBILITY_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_CHAIN_ADMISSIBILITY_TEST_SUPPORT_H

#include "nucleus/config.h"
#include "nucleus/capability.h"
#include "builder_result_test_support.h"

#include "nucleus/config_source/source_stack.h"
#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/inherit_declaration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <string>
#include <cstdint>
#include <utility>
#include <functional>

namespace nucleus::chain_admissibility_test {

// Factory invocations and pulls are counted separately because they answer
// different questions: a pull count alone would still read zero for a refused
// parent's ancestors even if the walker had constructed every one of them.
struct probe_log
{
    std::map<std::string, std::int32_t> build_count;
    std::map<std::string, std::int32_t> pull_count;
};

// Wraps an xml_source but reports a caller-supplied capability descriptor
// instead of xml_source's own, so a policy can be made to refuse exactly one
// document, and records every pull against the shared log.
struct probe_source
{
    xml_source                 src;
    std::string                name;
    capability_descriptor      caps;
    std::shared_ptr<probe_log> log;

    capability_descriptor capabilities() const { return caps; }

    void apply_projection(const schema_projection &projection)
    {
        src.apply_projection(projection);
    }

    inherit_declaration inheritance() const { return src.inheritance(); }

    config_source_result pull()
    {
        ++log->pull_count[name];
        return src.pull();
    }
};

struct probe_document
{
    std::string           text;
    capability_descriptor caps;
};

inline std::string probe_filename(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

inline std::function<source_handle(const std::string &)>
probe_factory(std::map<std::string, probe_document> documents, std::shared_ptr<probe_log> log)
{
    return [docs = std::move(documents), log](const std::string &path) -> source_handle
    {
        const std::string name = probe_filename(path);
        ++log->build_count[name];
        const auto it = docs.find(name);
        // An unlisted path becomes an unparsable document rather than a silent
        // success, so a mistyped name in a test fails loudly at pull time.
        const std::string text = (it == docs.end()) ? std::string{} : it->second.text;
        const capability_descriptor caps =
                (it == docs.end()) ? capability_descriptor{} : it->second.caps;
        return source_handle(probe_source{
                xml_source::from(xml_source_options::of_string(text)), name, caps, log});
    };
}

inline std::function<std::string(capability_descriptor)> reject_without_nesting()
{
    return [](capability_descriptor caps) -> std::string
    {
        return caps.supports(capability::nesting) ? std::string{} : "lacks nesting";
    };
}

// A three-link chain whose middle document is the only one lacking nesting, so
// the policy above refuses it and nothing beyond it may be reached.
inline std::map<std::string, probe_document> refused_middle_chain()
{
    const capability_descriptor bare{};
    const capability_descriptor nested{capability::nesting};
    return {
        {"grandparent.xml",
         {R"(<cluster><server name="web"><port>80</port></server></cluster>)", nested}},
        {"parent.xml",
         {R"(<cluster inherit="grandparent.xml"><server name="web" extend="wide">)"
          R"(<protocol>tcp</protocol></server></cluster>)", bare}},
        {"child.xml",
         {R"(<cluster inherit="parent.xml"><server name="web" extend="wide">)"
          R"(<port>82</port></server></cluster>)", nested}}};
}

inline config_space chain_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(element("protocol", anchor::keyspace("cluster/server"))));
    return nucleus::builder_result_test::built(builder);
}

}

#endif
