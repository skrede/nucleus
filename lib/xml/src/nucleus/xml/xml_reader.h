#ifndef HPP_GUARD_NUCLEUS_XML_XML_READER_H
#define HPP_GUARD_NUCLEUS_XML_XML_READER_H

#include <pugixml.hpp>

#include <memory>
#include <string>
#include <utility>
#include <string_view>

namespace nucleus::xml {

// A thin wrapper that owns a parsed pugixml document. This is the retained arena
// of the document-source view-node model: pugixml parses into its OWN contiguous
// pool, and node/attribute strings are null-terminated C-strings living inside
// that pool -- not pointers into the raw bytes we read off disk. So the views the
// xml source produces point into THIS object's document, and THIS object is the
// lifetime root that must outlive every such view. (For in-place parsing the raw
// buffer would also need to outlive the document; we parse from a copy held here,
// so the document alone is the root.)
//
// Held by shared_ptr inside a retained_buffer so a pulled batch pins it; dropped
// only after resolution copies typed values out.
class document_arena
{
public:
    // Loads XML from an in-memory buffer. The buffer is copied into pugixml's
    // pool during parse, so the caller's bytes need not outlive this object.
    // Returns the full parse result so the caller can distinguish an unreadable
    // file from a malformed document and surface pugixml's description.
    [[nodiscard]] pugi::xml_parse_result load_string(std::string_view text)
    {
        return m_document.load_buffer(text.data(), text.size());
    }

    [[nodiscard]] pugi::xml_parse_result load_file(const std::string &path)
    {
        return m_document.load_file(path.c_str());
    }

    [[nodiscard]] pugi::xml_node root() const { return m_document.document_element(); }

private:
    pugi::xml_document m_document;
};

}

#endif
