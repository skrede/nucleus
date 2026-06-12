#ifndef HPP_GUARD_NUCLEUS_TESTS_INHERITANCE_GOLDEN_GOLDEN_RUNNER_H
#define HPP_GUARD_NUCLEUS_TESTS_INHERITANCE_GOLDEN_GOLDEN_RUNNER_H

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>

// A thin, format-aware adapter that drives real fixture documents through the
// public load() surface and renders the resolved keyspace to a
// canonical text form an exact golden diff can gate on. It owns no host
// vocabulary: the shape it declares is a generic hierarchical document model, and
// the only third-party touch is the already-wrapped XML source.
namespace nucleus::golden {

// Declares the schema once: cluster(root) / server keyed by name /
// logger@log_level / profile / message / {greeting, description}. A config
// space carries exactly one primary key, so `server/name` is THE selector and
// `profile/name` is a plain leaf. Elements are registered parent-first so each
// anchor references an already-declared node.
inline void declare_schema(nucleus::config_space_builder &builder)
{
    using nucleus::anchor;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::element("logger", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
        nucleus::element("log_level", anchor::keyspace("cluster/server/logger"))));
    REQUIRE(builder.register_element(
        nucleus::element("profile", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
        nucleus::unique_element("name", anchor::keyspace("cluster/server/profile"))));
    REQUIRE(builder.register_element(
        nucleus::element("message", anchor::keyspace("cluster/server/profile"))));
    REQUIRE(builder.register_element(
        nucleus::element("greeting", anchor::keyspace("cluster/server/profile/message"))));
    REQUIRE(builder.register_element(
        nucleus::element("description", anchor::keyspace("cluster/server/profile/message"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("tag", anchor::keyspace("cluster/server"))));
}

// The filename portion of a (possibly absolute) path, so the factory can dispatch
// against an in-repo fixture directory without depending on the working directory.
inline std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// A document factory that maps every requested path to an XML file read from the
// given case directory. Dispatch is by filename so inherit= and multi-path stacks
// both resolve against the same fixture directory.
inline std::function<nucleus::source_handle(const std::string &)> file_factory(std::string case_dir)
{
    return [dir = std::move(case_dir)](const std::string &path) -> nucleus::source_handle {
        const std::string full = dir + "/" + filename_of(path);
        return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_file(full)));
    };
}

// Serializes a resolved config to one deterministic line per key, in the
// already-sorted keys() order. A scalar key emits `key = value [layer]`; a
// repeated path emits one indexed line per element, `key[i] = value [layer]`,
// each carrying its own per-element layer. Every line ends with a newline.
inline std::string serialize(const nucleus::config &config)
{
    std::string out;
    for(const std::string &key : config.keys())
    {
        const std::vector<std::string> values = config.get_all(key);
        const std::vector<nucleus::origin> *col = config.collection_provenance_of(key);
        if(col != nullptr)
        {
            // Repeated path: one line per element with its own layer label.
            for(std::size_t i = 0; i < values.size(); ++i)
            {
                const std::string layer =
                    i < col->size() ? (*col)[i].layer : std::string("unknown layer");
                out += key;
                out += "[";
                out += std::to_string(i);
                out += "] = ";
                out += values[i];
                out += " [";
                out += layer;
                out += "]\n";
            }
            continue;
        }

        // Scalar path: a single line with the winning origin's layer label.
        const nucleus::origin *orig = config.provenance_of(key);
        const std::string layer = orig != nullptr ? orig->layer : std::string("unknown layer");
        const std::string val = values.empty() ? std::string() : values.front();
        out += key;
        out += " = ";
        out += val;
        out += " [";
        out += layer;
        out += "]\n";
    }
    return out;
}

// Compares the golden against the actual serialization line by line. Returns the
// first differing line (1-based, with both sides) or nullopt when identical.
inline std::optional<std::string> diff(const std::string &expected, const std::string &actual)
{
    auto split = [](const std::string &text) {
        std::vector<std::string> lines;
        std::string current;
        // Tolerate CRLF: strip a trailing '\r' so a golden file checked out with
        // Windows line endings still compares equal to the LF-serialized actual.
        const auto flush = [&] {
            if(!current.empty() && current.back() == '\r')
                current.pop_back();
            lines.push_back(current);
            current.clear();
        };
        for(const char c : text)
        {
            if(c == '\n')
                flush();
            else
                current.push_back(c);
        }
        if(!current.empty())
            flush();
        return lines;
    };

    const std::vector<std::string> exp = split(expected);
    const std::vector<std::string> act = split(actual);
    const std::size_t n = std::max(exp.size(), act.size());
    for(std::size_t i = 0; i < n; ++i)
    {
        const std::string e = i < exp.size() ? exp[i] : std::string("<missing>");
        const std::string a = i < act.size() ? act[i] : std::string("<missing>");
        if(e != a)
            return std::string("line ") + std::to_string(i + 1)
                   + ": expected [" + e + "] but got [" + a + "]";
    }
    return std::nullopt;
}

}

#endif
