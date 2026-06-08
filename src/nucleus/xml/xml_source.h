#ifndef HPP_GUARD_NUCLEUS_XML_XML_SOURCE_H
#define HPP_GUARD_NUCLEUS_XML_XML_SOURCE_H

#include "nucleus/configuration_source/document_source.h"
#include "nucleus/configuration_source/inherit_declaration.h"

#include "nucleus/schema/projection.h"

#include <memory>
#include <string>
#include <utility>

namespace nucleus::xml {

// Forward declaration: the pugixml document arena. Defined in xml_reader.h
// (which pulls in pugixml). The shared_ptr member only needs an incomplete type
// here; the destructor and constructor are out-of-line in xml_source.cpp where
// xml_reader.h is included.
class document_arena;

// A document source backed by pugixml. It walks the parsed tree into keyspace
// entries: nested elements become `/`-separated key paths, and an element's
// attributes and pure-text leaf children become values. Every value is a VIEW
// into the document arena (the pugixml pool), and the returned batch pins that
// arena in its retained_buffer so the views stay valid until resolution copies
// them out.
//
// This is the ONLY place pugixml is reachable. The class is a normal
// document_source; nothing of pugixml appears in its interface, so the core never
// sees it.
class xml_source final : public document_source
{
public:
    // Parses XML directly from an in-memory string.
    [[nodiscard]] static xml_source from_string(std::string text)
    {
        return xml_source(kind::string, std::move(text));
    }

    // Parses XML from a file path at pull time.
    [[nodiscard]] static xml_source from_file(std::string path)
    {
        return xml_source(kind::file, std::move(path));
    }

    [[nodiscard]] capability_descriptor capabilities() const override;

    [[nodiscard]] configuration_source_result pull() override;

    // Retains the schema-derived projection the resolve fold hands over before
    // pull(), so the walk can render repeatable keyed containers (one instance
    // per primary-key value) instead of collapsing repeated siblings last-wins.
    void apply_projection(const schema_projection &projection) override
    {
        m_projection = projection;
    }

    // Returns the inheritance declaration read from the document root's inherit=
    // attribute. Callable after pull(); returns inherit_default when pull() has
    // not been called yet or the document root has no inherit= attribute.
    [[nodiscard]] inherit_declaration inheritance() const override;

private:
    enum class kind
    {
        string,
        file,
    };

    xml_source(kind source_kind, std::string input)
        : m_kind(source_kind), m_input(std::move(input))
    {
    }

    kind m_kind;
    std::string m_input;
    schema_projection m_projection;
    // Set during pull(); shared_ptr so inheritance() can read the root after
    // pull() returns without copying the arena.
    std::shared_ptr<document_arena> m_arena;
};

}

#endif
