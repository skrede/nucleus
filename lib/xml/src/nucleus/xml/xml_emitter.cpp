#include "nucleus/sources/xml_emitter.h"

#include "nucleus/config_emitter.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/keyspace/key_path.h"

#include <pugixml.hpp>

#include <string>
#include <vector>
#include <ostream>
#include <cstddef>
#include <string_view>

namespace nucleus::xml {

// The free functions and the stateless tag together model the output contract.
static_assert(config_emitter<emitter>,
              "nucleus::xml::emitter must model nucleus::config_emitter");

namespace {

// Escapes the five XML metacharacters in embedded text/attribute content. Element
// names are host-authored identifiers and are emitted verbatim; only the allowed-
// value annotation carries arbitrary content, so it is escaped here.
[[nodiscard]] std::string escape(std::string_view text)
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

[[nodiscard]] tree_node &child_of(std::vector<tree_node> &level, const std::string &name)
{
    for(tree_node &n : level)
        if(n.name == name)
            return n;
    level.push_back(tree_node{name, {}, {}});
    return level.back();
}

void emit_node(const tree_node &n, std::ostream &out, std::size_t depth)
{
    const std::string indent(depth * 2, ' ');
    out << indent << '<' << n.name;
    if(!n.allowed_values.empty())
    {
        std::string joined;
        for(std::size_t i = 0; i < n.allowed_values.size(); ++i)
        {
            if(i != 0)
                joined += '|';
            joined += n.allowed_values[i];
        }
        out << " allowed=\"" << escape(joined) << '"';
    }
    if(n.children.empty())
    {
        // Template only: a leaf carries no value, so it is self-closing.
        out << "/>\n";
        return;
    }
    out << ">\n";
    for(const tree_node &c : n.children)
        emit_node(c, out, depth + 1);
    out << indent << "</" << n.name << ">\n";
}

// Reuses an existing direct child element by name or appends a new one, so siblings
// of one parent share their parent node, reconstructing the nesting hierarchy from
// the flat '/'-separated keys.
[[nodiscard]] pugi::xml_node child_or_append(pugi::xml_node parent, const std::string &name)
{
    pugi::xml_node existing = parent.child(name.c_str());
    if(existing)
        return existing;
    return parent.append_child(name.c_str());
}

}

// Projects the sealed schema into a well-formed XML TEMPLATE: one element per
// declared field, nested by anchor path (a child is emitted inside its anchor's
// element), with a constrained field annotated by its allowed values. Deterministic
// hand-built projection (no values -- template only).
void emit_template(const configuration_space &space, std::ostream &out)
{
    std::vector<tree_node> roots;
    for(const schema_element &el : space.schema_elements())
    {
        tree_node *current = nullptr;
        std::vector<tree_node> *level = &roots;
        // Materialize the segments: declared_path() returns a temporary, so binding
        // a range-for to its segments() reference would dangle.
        const std::vector<std::string> segments = el.declared_path().segments();
        for(const std::string &segment : segments)
        {
            current = &child_of(*level, segment);
            level = &current->children;
        }
        if(current != nullptr && !el.allowed_values.empty())
            current->allowed_values = el.allowed_values;
    }

    if(roots.size() == 1)
    {
        emit_node(roots.front(), out, 0);
    }
    else
    {
        // Multiple (or zero) top-level keyspaces: wrap them in a single root.
        out << "<configuration>\n";
        for(const tree_node &r : roots)
            emit_node(r, out, 1);
        out << "</configuration>\n";
    }
}

// Projects a resolved configuration into a populated XML document: each '/'-separated
// key splits into element segments, intermediate segments are shared parent nodes,
// and the leaf is appended once per value so a repeated path persists ALL its values.
// A malformed key is skipped (never thrown), consistent with the read path.
void emit_document(const configuration &config, std::ostream &out)
{
    pugi::xml_document doc;
    for(const std::string &key : config.keys())
    {
        auto parsed = key_path::parse(key);
        if(!parsed)
            continue;

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
    doc.save(out, "  ");
}

}
