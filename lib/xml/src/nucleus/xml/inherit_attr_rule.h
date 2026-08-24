#ifndef HPP_GUARD_NUCLEUS_XML_INHERIT_ATTR_RULE_H
#define HPP_GUARD_NUCLEUS_XML_INHERIT_ATTR_RULE_H

#include "nucleus/error.h"
#include "nucleus/format.h"

#include "nucleus/config_source/config_source.h"

#include "nucleus/utility/escaped_text.h"

#include <string_view>

namespace nucleus {

// An inherit attribute that is present but empty is neither absence nor the
// opt-out keyword. Left alone it becomes a parent path of "", which is relative,
// so it is joined onto the declaring file's own directory and handed to the
// document factory as if a directory were a document.
inline expected<void, config_source_error>
check_inherit_attr(std::string_view attr_name,
                   std::string_view value,
                   std::string_view element)
{
    if(attr_name != "inherit" || !value.empty())
        return {};
    return unexpected(config_source_error{errc::malformed_source, nucleus::format(
        "inherit attribute on element '{}' names no parent; an inherit attribute "
        "must name a parent document or carry the opt-out keyword 'none' "
        "(an empty value would resolve to the declaring file's own directory)",
        escaped_text(element))});
}

}

#endif
