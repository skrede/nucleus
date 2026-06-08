#ifndef HPP_GUARD_NUCLEUS_XML_XML_WRITER_H
#define HPP_GUARD_NUCLEUS_XML_XML_WRITER_H

#include "nucleus/expected.h"

#include "nucleus/entry/configuration.h"

#include <string>
#include <variant>

namespace nucleus::xml {

// Persists a resolved configuration to a well-formed XML document. Both forms take
// ONLY core types -- no pugixml type appears in this interface; the pugixml-backed
// build lives entirely in xml_writer.cpp. The string form returns the serialized
// XML; the file form writes it to `path`. Each '/'-separated key becomes an element
// path; a repeated path persists ALL its values (one leaf element per value).
[[nodiscard]] expected<std::string, std::string> write_document(const configuration &config);

[[nodiscard]] expected<std::monostate, std::string>
write_document_to_file(const configuration &config, const std::string &path);

}

#endif
