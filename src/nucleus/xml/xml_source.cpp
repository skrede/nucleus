#include "xml_source.h"

#include "xml_reader.h"

#include "nucleus/capability.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/entry.h"

#include <pugixml.hpp>

#include <string>
#include <memory>
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

// Walks one element into entries under `path`. Attributes and pure-text leaf
// children become value entries; nested elements recurse. Every value is a
// string_view into the document arena -- never copied here -- so the batch must
// pin the arena (the caller does).
void walk(const pugi::xml_node &node, std::string_view path,
          const capability_descriptor &caps, std::vector<keyspace_entry> &out)
{
    for(const pugi::xml_attribute &attr : node.attributes())
    {
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
            out.push_back(make_entry(child_path,
                                     value::view(std::string_view(child.child_value())), caps));
            continue;
        }
        walk(child, child_path, caps, out);
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
    // element is addressable, matching the nested-element-as-path model.
    walk(root, std::string_view(root.name()), capabilities(), batch.entries);

    // Pin the arena: the entries' views point into it and must stay valid until
    // resolution copies them out. The handle owns the shared_ptr; dropping the
    // batch drops the arena.
    batch.buffer = retained_buffer(std::move(arena));
    return batch;
}

}
