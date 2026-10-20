#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_TEXT_RULES_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_TEXT_RULES_H

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_attach_rules.h"

#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/utility/escaped_text.h"
#include "nucleus/utility/display_controls.h"

#include <string>
#include <algorithm>
#include <functional>
#include <string_view>

namespace nucleus {

// Element names must not start with a digit so CLI flag text is unambiguously
// invertible back to a schema path (numeric leading chars would collide with
// ordinal index notation in the CLI bijection).
inline bool is_digit_led(std::string_view name)
{
    return !name.empty() && name.front() >= '0' && name.front() <= '9';
}

inline schema_attach_result check_name_not_digit_led(const schema_element &el)
{
    if(!is_digit_led(el.name))
        return {};
    return unexpected(nucleus::format(
            "schema element '{}' has a digit-led name: element names must not "
            "start with a digit (CLI flag disambiguation requires this)",
            escaped_text(el.name)));
}

// The declared path pushes this name onto the parent path unparsed, and the CLI surface pushes it into
// a completion word list that is EXPANDED and not merely split -- so a name is one bare, inert segment.
// Both shells draw a name exactly as they match or insert it, so a terminal control in one cannot be
// rendered away, only refused.
inline bool is_wellformed_name(const std::string &name)
{
    const bool clean  = !detail::has_display_control(name) && name.find_first_of(" !\"#$%&'()*:;<>?[\\]`{|}~") == std::string::npos;
    const auto parsed = key_path::parse(name);
    return clean && parsed && parsed->size() == 1;
}

inline bool is_element_name(const std::string &name)
{
    return !is_digit_led(name) && is_wellformed_name(name);
}

inline schema_attach_result check_name_wellformed(const schema_element &el)
{
    if(is_wellformed_name(el.name))
        return {};
    return unexpected(nucleus::format(
            "schema element '{}' has a malformed name: an element name is one path segment carrying no separator, space, control character or shell metacharacter",
            escaped_text(el.name)));
}

inline schema_attach_result check_element_name(const schema_element &el)
{
    if(auto r = check_name_not_digit_led(el); !r)
        return r;
    return check_name_wellformed(el);
}

// A registered schema path defines nodes whose segments project as flags exactly as element names do.
inline schema_attach_result check_schema_path(const std::string &path)
{
    const auto parsed = key_path::parse(path);
    if(!parsed)
        return unexpected(nucleus::format("schema path '{}' is not a well-formed key path: a schema path is non-empty segments joined by '/'",
                                          escaped_text(path)));
    const auto segment = std::ranges::find_if(parsed->segments(), std::not_fn(is_element_name));
    if(segment == parsed->segments().end())
        return {};
    return unexpected(nucleus::format(
            "schema path '{}' has a malformed segment '{}': each segment of a schema path meets the element-name rule",
            escaped_text(path), escaped_text(*segment)));
}

// An allowed value is a completion candidate inserted as drawn, the same as a name; a newline or NUL
// in one could not reach the bash word list intact either.
inline schema_attach_result check_allowed_values(const schema_element &el)
{
    const auto controlled = std::ranges::find_if(el.allowed_values, detail::has_display_control);
    if(controlled == el.allowed_values.end())
        return {};
    return unexpected(nucleus::format(
            "schema element '{}' has a malformed allowed value '{}': an allowed value carries no control character",
            escaped_text(el.name), escaped_text(*controlled)));
}

}

#endif
