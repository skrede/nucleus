#ifndef HPP_GUARD_NUCLEUS_XML_XML_EMITTER_H
#define HPP_GUARD_NUCLEUS_XML_XML_EMITTER_H

#include "nucleus/config.h"
#include "nucleus/config_space.h"
#include "nucleus/schema/projection.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace nucleus::xml {

// The XML projection of the format-agnostic output seam. Free-function call surface:
// emit_template turns a sealed space's declared schema into a blank XML template
// (one element per field, anchor-path nesting, an allowed= annotation on constrained
// leaves); emit_document turns a resolved config into a populated XML document
// (one leaf element per value, so repeated paths keep ALL their values). Both write
// into the caller's stream -- the user owns persistence. No pugixml type appears
// here; the library is reachable only in xml_emitter.cpp.
// When space_name is non-empty, the output is wrapped under <space_name>...</space_name>
// for symmetric round-trip with xml_source::with_space_name().
void emit_template(const config_space &space, std::ostream &out,
                   std::string_view space_name = {});
void emit_document(const config &config, std::ostream &out,
                   std::string_view space_name = {});
// Schema-aware overload: when proj is non-empty, pkey leaves are rendered as XML
// attributes on their parent container element rather than as child text nodes,
// preventing the double-write that would corrupt a load→emit→load round-trip.
void emit_document(const config &config, std::ostream &out,
                   const schema_projection &proj,
                   std::string_view space_name = {});

// The stateful emitter modeling nucleus::config_emitter: its members forward to the
// free functions above, so the xml module satisfies the output contract by type as
// well as by call surface. space_name is forwarded for named-space round-trips.
// proj carries optional schema-projection for pkey-attribute rendering; default-
// constructed (empty) proj produces schema-blind behavior identical to the old path.
struct emitter
{
    std::string space_name;
    schema_projection proj;

    void emit_template(const config_space &space, std::ostream &out) const
    {
        nucleus::xml::emit_template(space, out, space_name);
    }
    void emit_document(const config &config, std::ostream &out) const
    {
        nucleus::xml::emit_document(config, out, proj, space_name);
    }
};

}

#endif
