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

// Value-semantics options describing where an xml_source reads its document from:
// an in-memory XML string, or a file path read at pull time. This struct lives in
// the xml module and is reached by core ONLY through a host-supplied
// document_factory -- it is never referenced by any header under include/.
struct xml_source_options
{
    enum class input_kind
    {
        string,
        file,
    };

    input_kind  kind = input_kind::string;
    std::string data;  // XML text when kind==string; file path when kind==file

    [[nodiscard]] static xml_source_options of_string(std::string text)
    {
        return xml_source_options{input_kind::string, std::move(text)};
    }

    [[nodiscard]] static xml_source_options of_file(std::string path)
    {
        return xml_source_options{input_kind::file, std::move(path)};
    }
};

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
    // Builds an xml_source from a value-semantics options struct: parse from an
    // in-memory string or from a file path read at pull time.
    [[nodiscard]] static xml_source from(xml_source_options options)
    {
        const kind k = options.kind == xml_source_options::input_kind::file
                           ? kind::file : kind::string;
        return xml_source(k, std::move(options.data));
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
