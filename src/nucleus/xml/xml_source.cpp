#include "xml_reader.h"
#include "xml_source.h"

#include "nucleus/capability.h"

#include "nucleus/schema/projection.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <pugixml.hpp>

#include <memory>
#include <string>
#include <utility>
#include <string_view>

namespace nucleus::xml {

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

// True when an element is a pure-text leaf: no attributes, no child elements,
// exactly one pcdata child. Such an element models a `<key>value</key>` leaf, the
// element-form analog of an attribute.
bool is_text_leaf(const pugi::xml_node &node)
{
    return node.attributes().empty()
        && node.first_child()
        && node.first_child() == node.last_child()
        && node.first_child().type() == pugi::node_pcdata;
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
            && child.first_child().type() == pugi::node_pcdata)
        return std::string_view(child.child_value());

    return {};
}

// Walks one element into entries under `path`. Attributes and pure-text leaf
// children become value entries; nested elements recurse. Every value is a
// string_view into the document arena -- never copied here -- so the batch must
// pin the arena (the caller does).
//
// `proj` is the schema-derived projection: when a child element's path names a
// keyed container, its instances are placed under a transient path segment equal
// to the key value (so `<node name="yin"/>` and `<node name="yang"/>` become
// distinct `.../node/yin/...` and `.../node/yang/...` subtrees instead of one
// overwritten `.../node/...`). The key field itself is consumed -- suppressed via
// `skip` at the instance's own level -- since its value already names the segment.
void walk(const pugi::xml_node &node, std::string_view path,
          const capability_descriptor &caps, const schema_projection &proj,
          std::vector<keyspace_entry> &out, std::string_view skip = {})
{
    for(const pugi::xml_attribute &attr : node.attributes())
    {
        if(!skip.empty() && skip == std::string_view(attr.name()))
            continue;
        out.push_back(make_entry(join(path, attr.name()),
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
            out.push_back(make_entry(child_path,
                                     value::view(std::string_view(child.child_value())), caps));
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
                walk(child, join(child_path, key_val), caps, proj, out, *key);
                continue;
            }
        }

        walk(child, child_path, caps, proj, out);
    }
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

source_result xml_source::pull()
{
    auto arena = std::make_shared<document_arena>();

    const bool loaded = m_kind == kind::file ? arena->load_file(m_input)
                                             : arena->load_string(m_input);
    if(!loaded)
        return fail(std::string("xml source: failed to parse input"));

    pugi::xml_node root = arena->root();
    if(!root)
        return fail(std::string("xml source: document has no root element"));

    source_batch batch;
    // The root element name anchors the keyspace path so a document's top-level
    // element is addressable, matching the nested-element-as-path model. The
    // projection (empty unless the schema declared keyed containers) controls how
    // repeatable instances are distinguished.
    walk(root, std::string_view(root.name()), capabilities(), m_projection,
         batch.entries);

    // Pin the arena: the entries' views point into it and must stay valid until
    // resolution copies them out. The handle owns the shared_ptr; dropping the
    // batch drops the arena.
    batch.buffer = retained_buffer(std::move(arena));
    return batch;
}

}
