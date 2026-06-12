#include "nucleus/xml/xml_emitter.h"

#include "nucleus/config.h"
#include "nucleus/config_emitter.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include <pugixml.hpp>

#include <string>
#include <vector>
#include <ostream>
#include <cstddef>
#include <string_view>
#include <algorithm>
#include <tuple>

namespace nucleus::xml {

// The free functions and the stateless tag together model the output contract.
static_assert(config_emitter<emitter>,
              "nucleus::xml::emitter must model nucleus::config_emitter");

namespace {

// Escapes the five XML metacharacters in embedded text/attribute content. Element
// names are host-authored identifiers and are emitted verbatim; only the allowed-
// value annotation carries arbitrary content, so it is escaped here.
std::string escape(std::string_view text)
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

tree_node &child_of(std::vector<tree_node> &level, const std::string &name)
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
pugi::xml_node child_or_append(pugi::xml_node parent, const std::string &name)
{
    pugi::xml_node existing = parent.child(name.c_str());
    if(existing)
        return existing;
    return parent.append_child(name.c_str());
}

// Finds or appends the Nth child of `parent` with `name` (zero-based ordinal).
// Keys are sorted so ordinals ascend; when ordinal == existing count, a new sibling
// is appended; otherwise the last existing child with that name is returned (still
// within the same ordinal group, additional fields of the same instance).
pugi::xml_node indexed_child(pugi::xml_node parent,
                                            const std::string &name,
                                            std::size_t ordinal)
{
    std::size_t count = 0;
    pugi::xml_node last;
    for(pugi::xml_node c = parent.first_child(); c; c = c.next_sibling())
    {
        if(std::string(c.name()) == name)
        {
            last = c;
            ++count;
        }
    }
    if(count == ordinal)
        return parent.append_child(name.c_str());
    return last; // still within the same ordinal group
}

}

// Projects the sealed schema into a well-formed XML TEMPLATE: one element per
// declared field, nested by anchor path (a child is emitted inside its anchor's
// element), with a constrained field annotated by its allowed values. Deterministic
// hand-built projection (no values -- template only).
// When space_name is non-empty, all roots are wrapped under <space_name>...</space_name>
// for symmetric round-trip with xml_source::with_space_name().
void emit_template(const config_space &space, std::ostream &out,
                   std::string_view space_name)
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

    if(!space_name.empty())
    {
        // Named-space: always wrap in the space-name root regardless of root count.
        out << '<' << space_name << ">\n";
        for(const tree_node &r : roots)
            emit_node(r, out, 1);
        out << "</" << space_name << ">\n";
    }
    else if(roots.size() == 1)
    {
        emit_node(roots.front(), out, 0);
    }
    else
    {
        // Multiple (or zero) top-level keyspaces: wrap them in a single root.
        out << "<config>\n";
        for(const tree_node &r : roots)
            emit_node(r, out, 1);
        out << "</config>\n";
    }
}

namespace {

// Produces a sort key for a config map key so that numeric ordinals in indexed
// segments compare by value, not lexicographically. Each segment yields a
// (base_name, ordinal) pair; non-indexed segments use ordinal = 0.
// "cluster/node[10]/port" < "cluster/node[2]/port" lexicographically, but this
// key correctly orders node[2] before node[10].
std::vector<std::pair<std::string, std::size_t>>
numeric_sort_key(const std::string &key)
{
    std::vector<std::pair<std::string, std::size_t>> parts;
    std::size_t start = 0;
    for(std::size_t i = 0; i <= key.size(); ++i)
    {
        if(i == key.size() || key[i] == key_path::separator)
        {
            std::string_view seg(key.data() + start, i - start);
            if(key_path::is_indexed_segment(seg))
                parts.emplace_back(std::string(key_path::base_name(seg)),
                                   key_path::ordinal_of(seg));
            else
                parts.emplace_back(std::string(seg), std::size_t{0});
            start = i + 1;
        }
    }
    return parts;
}

} // namespace

// Projects a resolved config into a populated XML document: each '/'-separated
// key splits into element segments, intermediate segments are shared parent nodes,
// and the leaf is appended once per value so a repeated path persists ALL its values.
// Indexed scalar keys (e.g. "cluster/node[0]/port") produce N sibling elements for
// the repeated container (e.g. two <node> siblings in ordinal order). Bracket
// suffixes never appear in the output element names.
// A malformed key is skipped (never thrown), consistent with the read path.
// When space_name is non-empty, all top-level elements are re-parented under a new
// wrapper element named space_name for symmetric round-trip with with_space_name().
void emit_document(const config &config, std::ostream &out,
                   std::string_view space_name)
{
    pugi::xml_document doc;

    // Sort keys by numeric ordinal so indexed siblings are visited in 0, 1, 2, ...
    // order regardless of lexicographic map order (which puts node[10] before node[2]).
    std::vector<std::string> sorted_keys = config.keys();
    std::stable_sort(sorted_keys.begin(), sorted_keys.end(),
        [](const std::string &a, const std::string &b) {
            return numeric_sort_key(a) < numeric_sort_key(b);
        });

    for(const std::string &key : sorted_keys)
    {
        auto parsed = key_path::parse(key);
        if(!parsed)
            continue;

        const std::vector<std::string> &segments = parsed.value().segments();
        if(segments.empty())
            continue;

        // Descend through every segment but the leaf. For indexed segments (e.g.
        // "node[0]") use the base name as the element name and place/reuse the
        // correct ordinal sibling; for plain segments reuse or create normally.
        pugi::xml_node node = doc;
        for(std::size_t i = 0; i + 1 < segments.size(); ++i)
        {
            const std::string &seg = segments[i];
            if(key_path::is_indexed_segment(seg))
            {
                const std::string name = std::string(key_path::base_name(seg));
                node = indexed_child(node, name, key_path::ordinal_of(seg));
            }
            else
            {
                node = child_or_append(node, seg);
            }
        }

        // Emit the leaf. Indexed leaves (rare) strip the bracket suffix.
        // For plain repeated leaves the key is already canonical; get_all() gathers
        // all instances. For indexed-scalar keys (already a single instance) get()
        // returns exactly that instance's value directly.
        const std::string &leaf_seg = segments.back();
        const std::string leaf_name = key_path::is_indexed_segment(leaf_seg)
            ? std::string(key_path::base_name(leaf_seg))
            : leaf_seg;

        for(const std::string &value : config.get_all(key))
        {
            pugi::xml_node leaf_node = node.append_child(leaf_name.c_str());
            leaf_node.append_child(pugi::node_pcdata).set_value(value.c_str());
        }
    }

    if(!space_name.empty())
    {
        // Collect top-level children before mutation, then re-parent them under
        // the space-name wrapper. pugixml's append_move removes the node from its
        // current parent as it adds it to the new one.
        std::vector<pugi::xml_node> top_level;
        for(pugi::xml_node child = doc.first_child(); child; child = child.next_sibling())
            top_level.push_back(child);
        pugi::xml_node wrapper = doc.append_child(std::string(space_name).c_str());
        for(pugi::xml_node &child : top_level)
            wrapper.append_move(child);
    }

    doc.save(out, "  ");
}

}
