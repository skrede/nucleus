#include "xml_reader.h"
#include "nucleus/xml/xml_source.h"

#include "nucleus/format.h"
#include "nucleus/capability.h"
#include "nucleus/configuration_source/inherit_declaration.h"

#include "nucleus/schema/projection.h"

#include "nucleus/keyspace/entry.h"
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

// True when an element is a pure-text leaf: no attributes, no child elements,
// exactly one text child. Such an element models a `<key>value</key>` leaf, the
// element-form analog of an attribute.
bool is_text_leaf(const pugi::xml_node &node)
{
    return node.attributes().empty()
        && node.first_child()
        && node.first_child() == node.last_child()
        && is_value_node(node.first_child());
}

// The value of an element's primary-key field: the attribute named `key_field`,
// or a pure-text child element of that name. Empty when neither is present -- the
// element is an anonymous (keyless) instance of its container.
std::string_view keyed_value(const pugi::xml_node &node, const std::string &key_field)
{
    if(pugi::xml_attribute attr = node.attribute(key_field.c_str()))
        return std::string_view(attr.value());

    pugi::xml_node child = node.child(key_field.c_str());
    if(child && child.first_child()
            && child.first_child() == child.last_child()
            && is_value_node(child.first_child()))
        return std::string_view(child.child_value());

    return {};
}

// Validates grammar attributes on a transparent named-space root without emitting
// any keyspace entries. Accepts inherit= (consumed by inheritance()) and rejects
// extend= on the root (not a keyed instance). Other attributes pass silently --
// the root envelope carries metadata, not keyspace content.
expected<void, configuration_source_error>
validate_root_attrs(const pugi::xml_node &root)
{
    for(const pugi::xml_attribute &attr : root.attributes())
    {
        std::string_view name = std::string_view(attr.name());
        if(name == "inherit")
            continue; // consumed by inheritance()
        if(name == "extend")
            return unexpected(configuration_source_error{errc::malformed_source,
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
// the stack depth. 64 levels is far beyond any sane configuration document.
constexpr std::size_t max_element_depth = 64;

expected<void, configuration_source_error>
walk(const pugi::xml_node &node, std::string_view path,
     const capability_descriptor &caps, const schema_projection &proj,
     configuration_source_batch &batch, std::string_view skip,
     std::map<std::string, std::set<std::string>> &seen_keys,
     bool is_root, std::size_t depth)
{
    if(depth > max_element_depth)
        return unexpected(configuration_source_error{errc::malformed_source,
            nucleus::format(
                "element nesting exceeds the depth cap ({}) at '{}'",
                max_element_depth, path)});

    for(const pugi::xml_attribute &attr : node.attributes())
    {
        std::string_view attr_name = std::string_view(attr.name());

        // "inherit" is a grammar attribute: suppress on root (consumed by
        // inheritance()), reject loudly on non-root elements.
        if(attr_name == "inherit")
        {
            if(!is_root)
                return unexpected(configuration_source_error{errc::malformed_source,
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
                return unexpected(configuration_source_error{errc::malformed_source,
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
        if(is_text_leaf(child))
        {
            if(!skip.empty() && skip == std::string_view(child.name()))
                continue;
            batch.entries.push_back(make_entry(child_path,
                                               value::view(std::string_view(child.child_value())),
                                               caps));
            continue;
        }

        // A keyed container: distinguish this instance by its primary-key value
        // and consume the key field. A keyless instance (no key value) falls
        // through to the plain structural walk.
        if(const std::string *key = proj.key_of(child_path))
        {
            std::string_view key_val = keyed_value(child, *key);
            if(!key_val.empty())
            {
                // Same-layer duplicate primary-key detection: two instances with
                // the same key value in one document are an error.
                auto &seen = seen_keys[child_path];
                if(seen.count(std::string(key_val)))
                    return unexpected(configuration_source_error{errc::malformed_source,
                        nucleus::format(
                            "duplicate primary-key value '{}' in container '{}': "
                            "the same key value appears more than once in this document",
                            key_val, child_path)});
                seen.insert(std::string(key_val));

                // extend= on this instance element: parse the disposition.
                if(pugi::xml_attribute ext_attr = child.attribute("extend"))
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
                        return unexpected(configuration_source_error{errc::malformed_source,
                            nucleus::format(
                                "unknown extend value '{}' on element '{}'; "
                                "expected \"narrow\" or \"wide\"",
                                ext_val, child.name())});
                }

                if(auto r = walk(child, join(child_path, key_val), caps, proj,
                                 batch, *key, seen_keys, false, depth + 1); !r)
                    return r;
                continue;
            }
        }

        if(auto r = walk(child, child_path, caps, proj, batch, {}, seen_keys,
                         false, depth + 1); !r)
            return r;
    }

    return {};
}

}

capability_descriptor xml_source::capabilities() const
{
    // XML is tree-structured, preserves document order, allows repeated sibling
    // elements, and carries comments. It does not, on its own, type its scalars
    // (everything is text until a schema interprets it).
    return capability_descriptor{capability::nesting, capability::duplicate_keys,
                                 capability::comments, capability::ordering};
}

configuration_source_result xml_source::pull()
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
                return unexpected(configuration_source_error{errc::unreadable_source,
                    nucleus::format(
                        "xml source: cannot read file '{}': {}", m_input,
                        parsed.description())});
            return unexpected(configuration_source_error{errc::malformed_source,
                nucleus::format(
                    "xml source: failed to parse input: {} (at offset {})",
                    parsed.description(), parsed.offset)});
        }
    }

    pugi::xml_node root = m_arena->root();
    if(!root)
        return unexpected(configuration_source_error{errc::malformed_source,
            std::string("xml source: document has no root element")});

    configuration_source_batch batch;
    std::map<std::string, std::set<std::string>> seen_keys;

    if(!m_space_name.empty())
    {
        // Named-space envelope: validate root name, then walk each child directly
        // so the root element name is stripped from all key paths.
        if(std::string_view(root.name()) != m_space_name)
            return unexpected(configuration_source_error{errc::malformed_source,
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
                             batch, {}, seen_keys, false, 1); !r)
                return unexpected(r.error());
        }
    }
    else
    {
        // Unnamed space: root element name is the first key segment (unchanged behavior).
        if(auto r = walk(root, std::string_view(root.name()), capabilities(), m_projection,
                         batch, {}, seen_keys, true, 0); !r)
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
    pugi::xml_node root = m_arena->root();
    if(!root)
        return {};
    pugi::xml_attribute attr = root.attribute("inherit");
    if(!attr)
        return {}; // absence = inherit_default
    std::string_view val = attr.value();
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
