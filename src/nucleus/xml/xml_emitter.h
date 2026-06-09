#ifndef HPP_GUARD_NUCLEUS_XML_XML_EMITTER_H
#define HPP_GUARD_NUCLEUS_XML_XML_EMITTER_H

#include "nucleus/configuration_space.h"

#include "nucleus/entry/configuration.h"

#include <iosfwd>

namespace nucleus::xml {

// The XML projection of the format-agnostic output seam. Free-function call surface:
// emit_template turns a sealed space's declared schema into a blank XML template
// (one element per field, anchor-path nesting, an allowed= annotation on constrained
// leaves); emit_document turns a resolved configuration into a populated XML document
// (one leaf element per value, so repeated paths keep ALL their values). Both write
// into the caller's stream -- the user owns persistence. No pugixml type appears
// here; the library is reachable only in xml_emitter.cpp.
void emit_template(const configuration_space &space, std::ostream &out);
void emit_document(const configuration &config, std::ostream &out);

// The stateless emitter modeling nucleus::config_emitter: its members forward to the
// free functions above, so the xml module satisfies the output contract by type as
// well as by call surface.
struct emitter
{
    void emit_template(const configuration_space &space, std::ostream &out) const
    {
        nucleus::xml::emit_template(space, out);
    }
    void emit_document(const configuration &config, std::ostream &out) const
    {
        nucleus::xml::emit_document(config, out);
    }
};

}

#endif
