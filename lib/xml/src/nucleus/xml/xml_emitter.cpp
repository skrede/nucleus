#include "nucleus/xml/xml_emitter.h"

#include "nucleus/config.h"
#include "nucleus/format.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/projection.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/ordinal_sort_key.h"

#include <pugixml.hpp>

#include <string>
#include <vector>
#include <ostream>
#include <cstddef>
#include <string_view>
#include <algorithm>

namespace nucleus::xml {

namespace {

// Escapes the five XML metacharacters in embedded text/attribute content. Element
// names are not escaped -- they are validated against the XML Name production by
// is_valid_xml_name below; only the allowed-value annotation carries arbitrary
// content, so it is escaped here.
std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for(char const c : text)
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

bool is_name_start_byte(unsigned char b)
{
    return b >= 0x80
        || (b >= 'A' && b <= 'Z')
        || (b >= 'a' && b <= 'z')
        || b == '_' || b == ':';
}

bool is_name_byte(unsigned char b)
{
    return is_name_start_byte(b)
        || (b >= '0' && b <= '9')
        || b == '-' || b == '.';
}

// Validates a config key segment as an XML Name at the byte level: the first byte
// an ASCII NameStartChar or any high-bit (>= 0x80) UTF-8 byte, each later byte an
// ASCII NameChar or high-bit. High bytes are permitted un-decoded -- a deliberate
// tradeoff that accepts every legal international name without a UTF-8 decoder
// while still rejecting every hostile ASCII name (a space, '<', '"', a leading
// digit). [W3C XML 1.0 (Fifth Edition) 2.3, https://www.w3.org/TR/xml/#NT-Name]
bool is_valid_xml_name(std::string_view name)
{
    if(name.empty())
        return false;
    if(!is_name_start_byte(static_cast<unsigned char>(name.front())))
        return false;
    return std::all_of(name.begin() + 1, name.end(), [](char c) {
        return is_name_byte(static_cast<unsigned char>(c));
    });
}

// A byte a config value must not carry into XML text: the C0 control range (which
// XML 1.0 2.2 Char forbids) minus tab and newline. A bare carriage return (0x0D)
// falls in this range and is refused too -- it is legal XML but silently
// normalized to a newline on round-trip, which would mutate the value.
// [W3C XML 1.0 (Fifth Edition) 2.2]
bool is_forbidden_value_byte(char ch)
{
    auto const b = static_cast<unsigned char>(ch);
    if(b == 0x09 || b == 0x0A)
        return false;
    return b < 0x20;
}

expected<void, error> check_value_bytes(const std::string &key, std::string_view value)
{
    if(std::any_of(value.begin(), value.end(), is_forbidden_value_byte))
        return unexpected(error{errc::malformed_source, nucleus::format(
            "xml emit: value for key '{}' contains a control byte XML cannot "
            "represent (or a bare carriage return that would not round-trip)", key)});
    return {};
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

// Appends a config value as the character-data child of a leaf element. A value
// that is entirely XML whitespace (space/tab/newline) is written as a CDATA section
// rather than plain pcdata: pugixml's default parse flags discard whitespace-only
// pcdata, so a plain-text " " would reload as "". CDATA is retained verbatim and the
// reader accepts node_cdata, so the exact value round-trips. (A bare carriage return
// is already refused by check_value_bytes, so the whitespace set omits it.)
void append_value_text(pugi::xml_node leaf, const std::string &value)
{
    const bool whitespace_only =
        !value.empty() && value.find_first_not_of(" \t\n") == std::string::npos;
    leaf.append_child(whitespace_only ? pugi::node_cdata : pugi::node_pcdata)
        .set_value(value.c_str());
}

// Finds or appends the Nth child of `parent` with `name` (zero-based ordinal).
// Keys are sorted so ordinals ascend: ordinal == count appends a new sibling;
// ordinal < count returns the last existing sibling (a further field of the
// instance just emitted). ordinal > count is a gap -- a requested instance whose
// predecessors were never emitted -- and fails loudly: contiguity is the emit
// invariant and the emitter does not invent padding.
expected<pugi::xml_node, error> indexed_child(pugi::xml_node parent,
                                              const std::string &name,
                                              std::size_t ordinal,
                                              const std::string &container_path)
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
    if(ordinal > count)
        return unexpected(error{errc::malformed_source, nucleus::format(
            "xml emit: repeated container '{}' has a gap -- instance {} was "
            "requested but only {} contiguous instance(s) precede it; the emitter "
            "does not pad missing ordinals", container_path, ordinal, count)});
    if(ordinal == count)
        return parent.append_child(name.c_str());
    return last;
}

}

// Projects the sealed schema into a well-formed XML TEMPLATE: one element per
// declared field, nested by anchor path (a child is emitted inside its anchor's
// element), with a constrained field annotated by its allowed values. Deterministic
// hand-built projection (no values -- template only).
// When space_name is non-empty, all roots are wrapped under <space_name>...</space_name>
// for symmetric round-trip with xml_source::with_space_name().
expected<void, error> emit_template(const config_space &space, std::ostream &out,
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
    return {};
}

expected<void, error> emit_document(const config &config, std::ostream &out,
                   std::string_view space_name)
{
    return emit_document(config, out, schema_projection{}, space_name);
}

// Projects a resolved config into a populated XML document: each '/'-separated
// key splits into element segments, intermediate segments are shared parent nodes,
// and the leaf is appended once per value so a repeated path persists ALL its values.
// Indexed scalar keys (e.g. "cluster/node[0]/port") produce N sibling elements for
// the repeated container (e.g. two <node> siblings in ordinal order). Bracket
// suffixes never appear in the output element names.
// An unparseable key is skipped (never thrown), consistent with the read path;
// a key whose segments form an invalid XML name, a sparse ordinal, or a value
// carrying a control byte fails loudly through the emit channel.
// When space_name is non-empty, all top-level elements are re-parented under a new
// wrapper element named space_name for symmetric round-trip with with_space_name().
// With an empty space_name and a top-level element count other than one (zero or
// more than one), the roots are wrapped in a single <config> element (as
// emit_template does) so the output stays a well-formed single-root document the
// reader accepts.
// When proj is non-empty, pkey leaves are rendered as attributes on their parent
// container element (preventing double-write on round-trip); empty proj is schema-blind.
expected<void, error> emit_document(const config &config, std::ostream &out,
                   const schema_projection &proj,
                   std::string_view space_name)
{
    pugi::xml_document doc;

    // Sort keys by numeric ordinal so indexed siblings are visited in 0, 1, 2, ...
    // order regardless of lexicographic map order (which puts node[10] before node[2]).
    std::vector<std::string> sorted_keys = config.keys();
    std::stable_sort(sorted_keys.begin(), sorted_keys.end(),
        [](const std::string &a, const std::string &b) {
            return ordinal_sort_key(a) < ordinal_sort_key(b);
        });

    for(const std::string &key : sorted_keys)
    {
        auto parsed = key_path::parse(key);
        if(!parsed)
            continue;

        const std::vector<std::string> &segments = parsed.value().segments();
        if(segments.empty())
            continue;

        // Descend through every segment but the leaf. Track parent_canonical in
        // parallel (base names joined by separator) for the pkey attribute check.
        // NOLINTNEXTLINE(cppcoreguidelines-slicing): pugixml's xml_node is a non-owning handle; taking the document as a node view is the library's intended idiom and copies only the handle, not owned state.
        pugi::xml_node node = doc;
        std::string parent_canonical;
        for(std::size_t i = 0; i + 1 < segments.size(); ++i)
        {
            const std::string &seg = segments[i];
            const std::string base = std::string(key_path::base_name(seg));
            if(!parent_canonical.empty())
                parent_canonical += key_path::separator;
            parent_canonical += base;
            if(!is_valid_xml_name(base))
                return unexpected(error{errc::malformed_source, nucleus::format(
                    "xml emit: key segment '{}' (in key '{}') is not a valid XML "
                    "element name", base, key)});
            if(key_path::is_indexed_segment(seg))
            {
                auto child = indexed_child(node, base, key_path::ordinal_of(seg),
                                           parent_canonical);
                if(!child)
                    return unexpected(std::move(child).error());
                node = child.value();
            }
            else
                node = child_or_append(node, base);
        }

        // Emit the leaf. Indexed leaves (rare) strip the bracket suffix.
        // For plain repeated leaves the key is already canonical; get_all() gathers
        // all instances. For indexed-scalar keys (already a single instance) get()
        // returns exactly that instance's value directly.
        const std::string &leaf_seg = segments.back();
        const std::string leaf_name = key_path::is_indexed_segment(leaf_seg)
            ? std::string(key_path::base_name(leaf_seg))
            : leaf_seg;
        if(!is_valid_xml_name(leaf_name))
            return unexpected(error{errc::malformed_source, nucleus::format(
                "xml emit: key segment '{}' (in key '{}') is not a valid XML "
                "element/attribute name", leaf_name, key)});

        // When proj identifies this leaf as the pkey field of its parent container,
        // render it as an XML attribute on the parent node to prevent double-write.
        if(!proj.empty())
        {
            const std::string *pkey_field = proj.key_of(parent_canonical);
            if(pkey_field != nullptr && *pkey_field == leaf_name)
            {
                // A primary key renders as a single attribute on its parent. More
                // than one value -- several values under one key, or several indexed
                // keys sharing the parent -- would emit a repeated attribute, which
                // the reader refuses on re-read. Reject rather than emit a document
                // our own reader would reject.
                const std::vector<std::string> values = config.get_all(key);
                if(values.size() > 1 || node.attribute(leaf_name.c_str()))
                    return unexpected(error{errc::malformed_source, nucleus::format(
                        "xml emit: primary-key field '{}' on container '{}' carries "
                        "more than one value; a primary key renders as a single "
                        "attribute and a repeated attribute would be refused on "
                        "re-read", leaf_name, parent_canonical)});
                for(const std::string &value : values)
                {
                    if(auto ok = check_value_bytes(key, value); !ok)
                        return unexpected(std::move(ok).error());
                    node.append_attribute(leaf_name.c_str()).set_value(value.c_str());
                }
                continue;
            }
        }

        // A value on a path the schema declares a repeated CONTAINER (an element with
        // child elements anchored under it) is unrepresentable as element text: it
        // would emit a container carrying only character data, which the reader now
        // refuses. Refuse it here too so emit never produces what read cannot consume.
        // A repeated scalar LEAF is not a repeated container (the projection lists only
        // containers), so repeated leaves still emit their values.
        if(!proj.empty())
        {
            std::string leaf_canonical = parent_canonical;
            if(!leaf_canonical.empty())
                leaf_canonical.push_back(key_path::separator);
            leaf_canonical += leaf_name;
            if(proj.is_repeated_container(leaf_canonical))
                return unexpected(error{errc::malformed_source, nucleus::format(
                    "xml emit: key '{}' places a value on repeated container '{}', "
                    "which carries child elements, not character data",
                    key, leaf_canonical)});
        }

        // An indexed leaf carries its own ordinal (e.g. "cluster/tags[2]"). Route
        // it through indexed_child so the same contiguity invariant the container
        // path enforces applies here too: a gap fails loudly instead of silently
        // collapsing the ordinal to 0 on re-read.
        if(key_path::is_indexed_segment(leaf_seg))
        {
            std::string leaf_canonical = parent_canonical;
            if(!leaf_canonical.empty())
                leaf_canonical.push_back(key_path::separator);
            leaf_canonical += leaf_name;
            for(const std::string &value : config.get_all(key))
            {
                if(auto ok = check_value_bytes(key, value); !ok)
                    return unexpected(std::move(ok).error());
                auto leaf_node = indexed_child(node, leaf_name,
                                               key_path::ordinal_of(leaf_seg),
                                               leaf_canonical);
                if(!leaf_node)
                    return unexpected(std::move(leaf_node).error());
                append_value_text(leaf_node.value(), value);
            }
            continue;
        }

        for(const std::string &value : config.get_all(key))
        {
            if(auto ok = check_value_bytes(key, value); !ok)
                return unexpected(std::move(ok).error());
            const pugi::xml_node leaf_node = node.append_child(leaf_name.c_str());
            append_value_text(leaf_node, value);
        }
    }

    // Choose the wrapper element. A named space always wraps its roots (symmetric
    // with with_space_name()). An unnamed space wraps unless it has exactly one
    // top-level element -- exactly as emit_template does -- because neither a
    // multi-root document (a hidden second root the reader rejects) nor a rootless
    // one (only the XML declaration, which the reader refuses as "no root element")
    // is well-formed; both must become a single <config> root the reader accepts.
    std::string wrapper_name(space_name);
    if(wrapper_name.empty())
    {
        std::size_t top_level_elements = 0;
        for(pugi::xml_node child = doc.first_child(); child; child = child.next_sibling())
            if(child.type() == pugi::node_element)
                ++top_level_elements;
        if(top_level_elements != 1)
            wrapper_name = "config";
    }

    if(!wrapper_name.empty())
    {
        // Collect top-level children before mutation, then re-parent them under
        // the wrapper. pugixml's append_move removes the node from its current
        // parent as it adds it to the new one.
        std::vector<pugi::xml_node> top_level;
        for(pugi::xml_node child = doc.first_child(); child; child = child.next_sibling())
            top_level.push_back(child);
        pugi::xml_node wrapper = doc.append_child(wrapper_name.c_str());
        for(pugi::xml_node const &child : top_level)
            wrapper.append_move(child);
    }

    doc.save(out, "  ");
    return {};
}

}
