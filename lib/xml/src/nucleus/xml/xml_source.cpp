#include "xml_reader.h"
#include "nucleus/xml/xml_source.h"

#include "nucleus/format.h"
#include "nucleus/capability.h"
#include "nucleus/config_source/inherit_declaration.h"

#include "nucleus/schema/projection.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/value.h"

#include <pugixml.hpp>

#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>

namespace nucleus {

using xml::document_arena;

namespace {

// Computes the declared (schema-registered) path from a walk-time path that may
// contain both ordinal suffixes ("node[0]") and transient key-value segments
// inserted by the keyed-instance walk (e.g. "primary" in "cluster/server/primary/route").
// Strips ordinal suffixes from every segment AND skips key-value segments:
// a segment is a key value when the previous declared segment is a keyed container.
// After skipping a key value, the key container remains the current context so
// subsequent segments are appended under it (key values are purely transient).
std::string declared_path(std::string_view path, const schema_projection &proj)
{
    std::vector<std::string_view> raw_segs;
    std::size_t start = 0;
    for(std::size_t i = 0; i <= path.size(); ++i)
    {
        if(i == path.size() || path[i] == key_path::separator)
        {
            raw_segs.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }

    std::string result;
    std::string current;  // declared path built so far (for projection key lookups)
    bool skip_next = false; // true when the immediately next segment is a key value
    for(std::string_view const seg : raw_segs)
    {
        if(skip_next)
        {
            // This segment is the key value; discard it. current stays unchanged
            // so subsequent segments attach directly under the keyed container.
            skip_next = false;
            continue;
        }

        const std::string_view base = key_path::base_name(seg); // strips "[N]" suffix
        if(!result.empty())
            result.push_back(key_path::separator);
        result.append(base);

        if(!current.empty())
            current.push_back(key_path::separator);
        current.append(base);

        // If the declared path we just added is a keyed container, the immediately
        // following segment is its key value and must be skipped.
        skip_next = (proj.key_of(current) != nullptr);
    }
    return result;
}

// Joins a parent path and a child segment with the keyspace separator. The empty
// parent (document root) yields the bare segment.
std::string join(std::string_view parent, std::string_view segment)
{
    if(parent.empty())
        return std::string(segment);
    std::string out(parent);
    out.push_back('/');
    out.append(segment);
    return out;
}

// True when a node carries element text: plain character data or a CDATA
// section (the standard escape for values containing markup characters).
// pugixml's child_value() reads both.
bool is_value_node(const pugi::xml_node &node)
{
    return node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata;
}

// True when an element carries at least one child element. Such an element is
// structural (a container), never a text leaf.
bool has_element_child(const pugi::xml_node &node)
{
    for(const pugi::xml_node &child : node.children())
        if(child.type() == pugi::node_element)
            return true;
    return false;
}

// Reads a leaf element's value as the concatenation of all its text/CDATA
// children, per pugixml text()/DOM semantics: a comment or CDATA boundary splits
// character data into adjacent text nodes and every piece belongs to the value
// (`<port>8<!-- c -->080</port>` is 8080). A single text child (the common case)
// and the empty element are returned as zero-copy views into the arena; only a
// genuinely split value is materialized into an owned string.
value read_leaf_value(const pugi::xml_node &node)
{
    const pugi::xml_node first = node.first_child();
    if(!first || (first == node.last_child() && is_value_node(first)))
        return value::view(std::string_view(node.child_value()));

    std::string joined;
    for(const pugi::xml_node &child : node.children())
        if(is_value_node(child))
            joined.append(child.value());
    return value::owned(std::move(joined));
}

// The value of an element's primary-key field: the attribute named `key_field`,
// or a text child element of that name (whose value may be split across text and
// CDATA nodes). A keyless instance -- neither present -- yields an empty value.
value keyed_value(const pugi::xml_node &node, const std::string &key_field)
{
    if(pugi::xml_attribute const attr = node.attribute(key_field.c_str()))
        return value::view(std::string_view(attr.value()));

    pugi::xml_node const child = node.child(key_field.c_str());
    if(child && !has_element_child(child) && child.first_child())
        return read_leaf_value(child);

    return value::view(std::string_view{});
}

// Validates grammar attributes on a transparent named-space root without emitting
// any keyspace entries. Accepts inherit= (consumed by inheritance()) and rejects
// extend= on the root (not a keyed instance). Other attributes pass silently --
// the root envelope carries metadata, not keyspace content.
expected<void, config_source_error>
validate_root_attrs(const pugi::xml_node &root)
{
    std::set<std::string_view> seen_attrs;
    for(const pugi::xml_attribute &attr : root.attributes())
    {
        auto const name = std::string_view(attr.name());
        if(!seen_attrs.insert(name).second)
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "duplicate attribute '{}' on element '{}': "
                    "the same attribute appears more than once on this element",
                    name, root.name())});
        if(name == "inherit")
            continue; // consumed by inheritance()
        if(name == "extend")
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "extend attribute is not permitted on element '{}'; "
                    "it is only valid on a primary-keyed container instance",
                    root.name())});
    }
    return {};
}

// Walks one element into keyspace entries under `path`. Attributes and pure-text
// leaf children become value entries; nested elements recurse. Every value is a
// string_view into the document arena -- never copied here -- so the batch must
// pin the arena (the caller does).
//
// `proj` is the schema-derived projection: when a child element's path names a
// keyed container, its instances are placed under a transient path segment equal
// to the key value (so `<server name="primary"/>` and `<server name="secondary"/>` become
// distinct `.../server/primary/...` and `.../server/secondary/...` subtrees instead of one
// overwritten `.../server/...`). The key field itself is consumed -- suppressed via
// `skip` at the instance's own level -- since its value already names the segment.
//
// Grammar attributes handled here:
//   inherit=  -- valid only on the document root element (is_root=true); absent on
//                 root is fine (suppressed, not emitted). On a non-root element it
//                 is a loud parse error naming the element.
//   extend=   -- valid only on keyed container instances; absent is fine. On any
//                 other element it is a loud parse error.
//
// seen_keys accumulates (container_path -> {key_values}) to detect same-document
// duplicate primary-key values and fail loudly. The map is shared across all
// recursive calls.
//
// batch is shared so the keyed-instance branch can push extend dispositions.
// Hostile or runaway nesting must produce a loud error, not a stack overflow:
// the walk recurses once per element level, so an unbounded document controls
// the stack depth. 64 levels is far beyond any sane config document.
constexpr std::size_t max_element_depth = 64;

expected<void, config_source_error>
walk(const pugi::xml_node &node, std::string_view path,
     const capability_descriptor &caps, const schema_projection &proj,
     config_source_batch &batch, std::string_view skip,
     std::map<std::string, std::set<std::string>> &seen_keys,
     std::map<std::string, std::size_t> &ordinal_counters,
     bool is_root, std::size_t depth)
{
    if(depth > max_element_depth)
        return unexpected(config_source_error{errc::malformed_source,
            nucleus::format(
                "element nesting exceeds the depth cap ({}) at '{}'",
                max_element_depth, path)});

    // Mixed content -- non-whitespace character data alongside a child element --
    // is not a modeled configuration shape (a value is either a leaf's text or a
    // container's children, never both). Reject it rather than silently dropping
    // the text via the structural walk. Whitespace between elements is discarded
    // by pugixml's default parse flags and so never trips this.
    {
        bool has_child_element = false;
        bool has_text = false;
        for(const pugi::xml_node &content : node.children())
        {
            if(content.type() == pugi::node_element)
                has_child_element = true;
            else if(is_value_node(content)
                    && std::string_view(content.value())
                           .find_first_not_of(" \t\r\n") != std::string_view::npos)
                has_text = true;
        }
        if(has_child_element && has_text)
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "element '{}' mixes character data and child elements; "
                    "mixed content is not a supported configuration shape",
                    node.name())});
    }

    std::set<std::string_view> seen_attrs;
    for(const pugi::xml_attribute &attr : node.attributes())
    {
        auto const attr_name = std::string_view(attr.name());

        if(!seen_attrs.insert(attr_name).second)
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "duplicate attribute '{}' on element '{}': "
                    "the same attribute appears more than once on this element",
                    attr_name, node.name())});

        // "inherit" is a grammar attribute: suppress on root (consumed by
        // inheritance()), reject loudly on non-root elements.
        if(attr_name == "inherit")
        {
            if(!is_root)
                return unexpected(config_source_error{errc::malformed_source,
                    nucleus::format(
                        "inherit attribute is not permitted on element '{}'; "
                        "it is only valid on the document root element",
                        node.name())});
            continue; // root: skip silently -- inheritance() reads m_arena
        }

        // "extend" is a grammar attribute valid only on primary-keyed container
        // instances. When skip is non-empty this node is a keyed instance (the
        // caller already extracted and recorded the extend disposition before
        // recursing); suppress the attribute so it is not emitted to keyspace
        // entries. When skip is empty this node is NOT a keyed instance and the
        // attribute is a user error.
        if(attr_name == "extend")
        {
            if(skip.empty())
                return unexpected(config_source_error{errc::malformed_source,
                    nucleus::format(
                        "extend attribute is not permitted on element '{}'; "
                        "it is only valid on a primary-keyed container instance",
                        node.name())});
            continue; // keyed instance: suppress from entries (already consumed by caller)
        }

        if(!skip.empty() && skip == attr_name)
            continue;
        batch.entries.push_back(make_entry(join(path, attr.name()),
                                           value::view(std::string_view(attr.value())), caps));
    }

    for(const pugi::xml_node &child : node.children())
    {
        if(child.type() != pugi::node_element)
            continue;

        std::string child_path = join(path, child.name());

        // A leaf: no attributes and no child elements. Its value is the
        // concatenation of any text/CDATA children, or the empty string when it
        // has none (`<motd></motd>` -> ""). A repeated container is structural
        // even when momentarily empty, so it is never mistaken for a leaf.
        if(child.attributes().empty() && !has_element_child(child)
           && !proj.is_repeated_container(declared_path(child_path, proj)))
        {
            if(!skip.empty() && skip == std::string_view(child.name()))
                continue;
            batch.entries.push_back(make_entry(child_path, read_leaf_value(child), caps));
            continue;
        }

        // A keyed container: distinguish this instance by its primary-key value
        // and consume the key field. A keyless instance (no key value) falls
        // through to the plain structural walk.
        if(const std::string *key = proj.key_of(child_path))
        {
            const value key_value = keyed_value(child, *key);
            const std::string_view key_val = key_value.text();
            if(!key_val.empty())
            {
                // Same-layer duplicate primary-key detection: two instances with
                // the same key value in one document are an error.
                auto &seen = seen_keys[child_path];
                if(seen.contains(std::string(key_val)))
                    return unexpected(config_source_error{errc::malformed_source,
                        nucleus::format(
                            "duplicate primary-key value '{}' in container '{}': "
                            "the same key value appears more than once in this document",
                            key_val, child_path)});
                seen.insert(std::string(key_val));

                // extend= on this instance element: parse the disposition.
                if(pugi::xml_attribute const ext_attr = child.attribute("extend"))
                {
                    std::string_view ext_val = ext_attr.value();
                    if(ext_val == "narrow")
                        batch.dispositions.push_back({std::string(child_path),
                                                      std::string(key_val),
                                                      extend_strength::narrow});
                    else if(ext_val == "wide")
                        batch.dispositions.push_back({std::string(child_path),
                                                      std::string(key_val),
                                                      extend_strength::wide});
                    else
                        return unexpected(config_source_error{errc::malformed_source,
                            nucleus::format(
                                "unknown extend value '{}' on element '{}'; "
                                "expected \"narrow\" or \"wide\"",
                                ext_val, child.name())});
                }

                // Emit the pkey field as an ordinary leaf so it survives slice.
                // The recursive walk will still suppress it via skip, but
                // this entry is already recorded before suppression fires.
                batch.entries.push_back(make_entry(
                    join(join(child_path, key_val), *key),
                    key_value, caps));

                if(auto r = walk(child, join(child_path, key_val), caps, proj,
                                 batch, *key, seen_keys, ordinal_counters,
                                 false, depth + 1); !r)
                    return r;
                continue;
            }
        }

        // Repeated container: assign a zero-based ordinal per sibling occurrence
        // in document order. Strips ordinal suffixes and key-value segments from
        // child_path for the projection lookup since the schema stores declared paths.
        // extend= on a repeated container is recorded as a disposition (the fold
        // will reject it as a layering_violation); it must not surface as an
        // attribute entry in the keyspace.
        if(proj.is_repeated_container(declared_path(child_path, proj)))
        {
            static constexpr const char *kExtend = "extend";
            if(pugi::xml_attribute const ext_attr = child.attribute(kExtend))
            {
                // Record a sentinel disposition using the declared path (key segments
                // stripped) so fold() can match it against repeated_container_prefixes.
                batch.dispositions.push_back(
                    {declared_path(child_path, proj), {}, extend_strength::narrow});
                (void)ext_attr; // value ignored; fold rejects all extend= on repeated
            }
            std::size_t &ordinal = ordinal_counters[child_path];
            std::string const indexed_path =
                child_path + "[" + std::to_string(ordinal++) + "]";
            // Pass "extend" as skip so the recursive walk suppresses the grammar
            // attribute rather than treating it as an unknown entry.
            if(auto r = walk(child, indexed_path, caps, proj, batch,
                             kExtend, seen_keys, ordinal_counters,
                             false, depth + 1); !r)
                return r;
            continue;
        }

        if(auto r = walk(child, child_path, caps, proj, batch, {}, seen_keys,
                         ordinal_counters, false, depth + 1); !r)
            return r;
    }

    return {};
}

}

capability_descriptor xml_source::capabilities() 
{
    // XML is tree-structured, preserves document order, allows repeated sibling
    // elements, and carries comments. It does not, on its own, type its scalars
    // (everything is text until a schema interprets it).
    return capability_descriptor{capability::nesting, capability::duplicate_keys,
                                 capability::comments, capability::ordering};
}

config_source_result xml_source::pull()
{
    // Parse the document at most once: if the arena is already populated from a
    // prior pull() (e.g., a chain-walk discovery pull followed by a fold pull),
    // skip the file-read/parse step and re-walk the cached DOM. The projection can
    // change between pulls (the fold hands the schema projection in via
    // apply_projection() before each pull); only the parse result is cached.
    if(!m_arena)
    {
        m_arena = std::make_shared<document_arena>();
        const pugi::xml_parse_result parsed =
            m_kind == kind::file ? m_arena->load_file(m_input)
                                 : m_arena->load_string(m_input);
        if(parsed.status != pugi::status_ok)
        {
            m_arena.reset();
            if(parsed.status == pugi::status_file_not_found
               || parsed.status == pugi::status_io_error)
                return unexpected(config_source_error{errc::unreadable_source,
                    nucleus::format(
                        "xml source: cannot read file '{}': {}", m_input,
                        parsed.description())});
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "xml source: failed to parse input: {} (at offset {})",
                    parsed.description(), parsed.offset)});
        }
    }

    pugi::xml_node const root = m_arena->root();
    if(!root)
        return unexpected(config_source_error{errc::malformed_source,
            std::string("xml source: document has no root element")});

    // XML 1.0 permits exactly one root element and no character data after it.
    // pugixml is lenient: it parses trailing sibling elements into the document
    // (a hidden second root) and retains trailing CDATA. Reject both here, once,
    // so the named-space and unnamed walks below are covered. (Trailing plain
    // text is discarded by pugixml before it reaches the tree, so it cannot enter
    // the keyspace; only retained top-level nodes are observable here.)
    for(pugi::xml_node sib = root.next_sibling(); sib; sib = sib.next_sibling())
    {
        if(sib.type() == pugi::node_element)
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "xml source: document has more than one root element "
                    "(a second root element '{}' follows the root element '{}')",
                    sib.name(), root.name())});
        if(is_value_node(sib)
           && std::string_view(sib.value()).find_first_not_of(" \t\r\n")
                  != std::string_view::npos)
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format(
                    "xml source: trailing content after the root element '{}'",
                    root.name())});
    }

    config_source_batch batch;
    std::map<std::string, std::set<std::string>> seen_keys;
    std::map<std::string, std::size_t> ordinal_counters;

    if(!m_space_name.empty())
    {
        // Named-space envelope: validate root name, then walk each child directly
        // so the root element name is stripped from all key paths.
        if(std::string_view(root.name()) != m_space_name)
            return unexpected(config_source_error{errc::malformed_source,
                nucleus::format("xml source: expected root element '{}', found '{}'",
                    m_space_name, root.name())});

        // Validate root grammar attributes (inherit= suppressed, anything else
        // that is not a known grammar attr is a walk error) without emitting entries.
        if(auto r = validate_root_attrs(root); !r)
            return unexpected(r.error());

        // Walk each direct child as a top-level keyspace entry (root is transparent).
        for(const pugi::xml_node &child : root.children())
        {
            if(child.type() != pugi::node_element)
                continue;
            if(auto r = walk(child, std::string_view(child.name()), capabilities(), m_projection,
                             batch, {}, seen_keys, ordinal_counters, false, 1); !r)
                return unexpected(r.error());
        }
    }
    else
    {
        // Unnamed space: root element name is the first key segment (unchanged behavior).
        if(auto r = walk(root, std::string_view(root.name()), capabilities(), m_projection,
                         batch, {}, seen_keys, ordinal_counters, true, 0); !r)
            return unexpected(r.error());
    }

    // Pin the arena: the entries' views point into it and must stay valid until
    // resolution copies them out. m_arena is also kept alive as a member so
    // inheritance() can read the root after pull() returns.
    batch.buffer = retained_buffer(m_arena);
    return batch;
}

inherit_declaration xml_source::inheritance() const
{
    if(!m_arena)
        return {}; // pull() not yet called; safe default (inherit_default)
    pugi::xml_node const root = m_arena->root();
    if(!root)
        return {};
    pugi::xml_attribute const attr = root.attribute("inherit");
    if(!attr)
        return {}; // absence = inherit_default
    std::string_view const val = attr.value();
    if(val == "none")
    {
        inherit_declaration d;
        d.which = inherit_declaration::kind::opt_out;
        return d;
    }
    inherit_declaration d;
    d.which = inherit_declaration::kind::parent_path;
    d.path = std::string(val);
    return d;
}

}
