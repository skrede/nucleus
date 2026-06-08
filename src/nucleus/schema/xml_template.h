#ifndef HPP_GUARD_NUCLEUS_SCHEMA_XML_TEMPLATE_H
#define HPP_GUARD_NUCLEUS_SCHEMA_XML_TEMPLATE_H

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus {

namespace xml_template_detail {

// Escapes the five XML metacharacters in embedded text/attribute content. Element
// names are host-authored identifiers and are emitted verbatim; only the
// allowed-value annotation carries arbitrary content, so it is escaped here.
[[nodiscard]] inline std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for(char c : text)
    {
        switch(c)
        {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// One tree_node of the reconstructed element tree: a path segment, the allowed-value
// set if a constrained element was declared exactly here, and ordered children.
struct tree_node
{
    std::string name;
    std::vector<std::string> allowed_values;
    std::vector<tree_node> children;
};

[[nodiscard]] inline tree_node &child_of(std::vector<tree_node> &level, const std::string &name)
{
    for(tree_node &n : level)
        if(n.name == name)
            return n;
    level.push_back(tree_node{name, {}, {}});
    return level.back();
}

inline void emit(const tree_node &n, std::string &out, std::size_t depth)
{
    const std::string indent(depth * 2, ' ');
    out += indent;
    out += '<';
    out += n.name;
    if(!n.allowed_values.empty())
    {
        std::string joined;
        for(std::size_t i = 0; i < n.allowed_values.size(); ++i)
        {
            if(i != 0)
                joined += '|';
            joined += n.allowed_values[i];
        }
        out += " allowed=\"";
        out += escape(joined);
        out += '"';
    }
    if(n.children.empty())
    {
        // Template only: a leaf carries no value, so it is self-closing.
        out += "/>\n";
        return;
    }
    out += ">\n";
    for(const tree_node &c : n.children)
        emit(c, out, depth + 1);
    out += indent;
    out += "</";
    out += n.name;
    out += ">\n";
}

}

// Projects a sealed schema into a well-formed XML TEMPLATE string: one element per
// declared field, nested by anchor path (a child is emitted inside its anchor's
// element), with a constrained field annotated by its allowed values. Hand-built
// like the completion generator -- a deterministic string with NO XML library and
// no default/placeholder values (template only).
[[nodiscard]] inline std::string emit_xml_template(const schema_registry &schema)
{
    using xml_template_detail::tree_node;

    std::vector<tree_node> roots;
    for(const schema_element &el : schema.elements())
    {
        tree_node *current = nullptr;
        std::vector<tree_node> *level = &roots;
        // Materialize the segments: declared_path() returns a temporary, so binding
        // a range-for to its segments() reference would dangle.
        const std::vector<std::string> segments = el.declared_path().segments();
        for(const std::string &segment : segments)
        {
            current = &xml_template_detail::child_of(*level, segment);
            level = &current->children;
        }
        if(current != nullptr && !el.allowed_values.empty())
            current->allowed_values = el.allowed_values;
    }

    std::string out;
    if(roots.size() == 1)
    {
        xml_template_detail::emit(roots.front(), out, 0);
    }
    else
    {
        // Multiple (or zero) top-level keyspaces: wrap them in a single root.
        out += "<configuration>\n";
        for(const tree_node &r : roots)
            xml_template_detail::emit(r, out, 1);
        out += "</configuration>\n";
    }
    return out;
}

}

#endif
