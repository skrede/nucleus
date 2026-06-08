#include "xml_writer.h"

#include "nucleus/format.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/keyspace/key_path.h"

#include <pugixml.hpp>

#include <string>
#include <vector>
#include <sstream>
#include <optional>

namespace nucleus::xml {

namespace {

// Finds a direct child element by name, or appends a new one. Reusing an existing
// child makes siblings of one parent share their parent node, reconstructing the
// nesting hierarchy from the flat '/'-separated keys.
[[nodiscard]] pugi::xml_node child_or_append(pugi::xml_node parent, const std::string &name)
{
    pugi::xml_node existing = parent.child(name.c_str());
    if(existing)
        return existing;
    return parent.append_child(name.c_str());
}

// Builds the document from the configuration's keys: each key splits into its path
// segments; intermediate segments are shared element nodes, and the leaf segment is
// appended once per value so a repeated path persists ALL its values (no last-wins
// loss). Returns an error string on a malformed key, else nullopt.
[[nodiscard]] std::optional<std::string>
build_document(const configuration &config, pugi::xml_document &doc)
{
    for(const std::string &key : config.keys())
    {
        auto parsed = key_path::parse(key);
        if(!parsed)
            return nucleus::format("xml writer: malformed key '{}'", key);

        const std::vector<std::string> &segments = parsed.value().segments();
        if(segments.empty())
            continue;

        // Descend (creating/reusing) through every segment but the leaf; the first
        // segment is the document root element.
        pugi::xml_node node = doc;
        for(std::size_t i = 0; i + 1 < segments.size(); ++i)
            node = child_or_append(node, segments[i]);

        // The leaf is appended once per value -- pugixml escapes the text on save.
        const std::string &leaf = segments.back();
        for(const std::string &value : config.get_all(key))
        {
            pugi::xml_node leaf_node = node.append_child(leaf.c_str());
            leaf_node.append_child(pugi::node_pcdata).set_value(value.c_str());
        }
    }
    return std::nullopt;
}

}

expected<std::string, std::string> write_document(const configuration &config)
{
    pugi::xml_document doc;
    if(auto error = build_document(config, doc); error)
        return unexpected(std::move(*error));

    std::ostringstream out;
    doc.save(out, "  ");
    return out.str();
}

expected<std::monostate, std::string>
write_document_to_file(const configuration &config, const std::string &path)
{
    pugi::xml_document doc;
    if(auto error = build_document(config, doc); error)
        return unexpected(std::move(*error));

    if(!doc.save_file(path.c_str(), "  "))
        return unexpected(nucleus::format("xml writer: could not write file '{}'", path));
    return std::monostate{};
}

}
