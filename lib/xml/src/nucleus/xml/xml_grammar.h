#ifndef HPP_GUARD_NUCLEUS_XML_XML_GRAMMAR_H
#define HPP_GUARD_NUCLEUS_XML_XML_GRAMMAR_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include <span>
#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <algorithm>
#include <string_view>

namespace nucleus::xml {

struct code_point_range
{
    std::uint32_t low;
    std::uint32_t high;
};

// XML 1.0 Fifth Edition, NameStartChar: https://www.w3.org/TR/xml/#NT-NameStartChar
inline constexpr std::array<code_point_range, 16> xml_name_start_ranges{
        {{':', ':'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}, {0x00C0, 0x00D6}, {0x00D8, 0x00F6}, {0x00F8, 0x02FF}, {0x0370, 0x037D}, {0x037F, 0x1FFF}, {0x200C, 0x200D}, {0x2070, 0x218F}, {0x2C00, 0x2FEF}, {0x3001, 0xD7FF}, {0xF900, 0xFDCF}, {0xFDF0, 0xFFFD}, {0x10000, 0xEFFFF}}};

// The ranges NameChar adds to NameStartChar: https://www.w3.org/TR/xml/#NT-NameChar
inline constexpr std::array<code_point_range, 6> xml_name_extra_ranges{
        {{'-', '-'}, {'.', '.'}, {'0', '9'}, {0x00B7, 0x00B7}, {0x0300, 0x036F}, {0x203F, 0x2040}}};

// XML 1.0 Fifth Edition, Char: https://www.w3.org/TR/xml/#NT-Char
inline constexpr std::array<code_point_range, 6> xml_char_ranges{
        {{0x09, 0x09}, {0x0A, 0x0A}, {0x0D, 0x0D}, {0x0020, 0xD7FF}, {0xE000, 0xFFFD}, {0x10000, 0x10FFFF}}};

struct utf8_shape
{
    std::uint8_t lead_low;
    std::uint8_t lead_high;
    std::uint8_t first_tail_low;
    std::uint8_t first_tail_high;
    std::size_t  tail_count;
};

// Unicode 15.0, table 3-7 "Well-Formed UTF-8 Byte Sequences". The lead byte fixes
// both the sequence length and the admissible range of the first continuation, so
// overlong forms, UTF-16 surrogates and code points above U+10FFFF are rejected by
// the shape itself rather than by a second pass over the decoded value.
inline constexpr std::array<utf8_shape, 9> utf8_shapes{
        {{0x00, 0x7F, 0x00, 0x00, 0},
         {0xC2, 0xDF, 0x80, 0xBF, 1},
         {0xE0, 0xE0, 0xA0, 0xBF, 2},
         {0xE1, 0xEC, 0x80, 0xBF, 2},
         {0xED, 0xED, 0x80, 0x9F, 2},
         {0xEE, 0xEF, 0x80, 0xBF, 2},
         {0xF0, 0xF0, 0x90, 0xBF, 3},
         {0xF1, 0xF3, 0x80, 0xBF, 3},
         {0xF4, 0xF4, 0x80, 0x8F, 3}}};

struct utf8_code_point
{
    std::uint32_t value;
    std::size_t   length;
};

inline bool in_ranges(std::span<const code_point_range> ranges,
                      std::uint32_t                     value) noexcept
{
    return std::any_of(ranges.begin(), ranges.end(),
                       [value](const code_point_range &range)
                       { return value >= range.low && value <= range.high; });
}

inline std::optional<std::uint32_t> decode_utf8_tail(std::string_view  text,
                                                     std::size_t       offset,
                                                     const utf8_shape &shape,
                                                     std::uint32_t     value) noexcept
{
    for(std::size_t index = 1; index <= shape.tail_count; ++index)
    {
        const auto         tail = static_cast<std::uint8_t>(text[offset + index]);
        const std::uint8_t low  = index == 1 ? shape.first_tail_low : 0x80;
        const std::uint8_t high = index == 1 ? shape.first_tail_high : 0xBF;
        if(tail < low || tail > high)
            return std::nullopt;
        value = (value << 6) | (tail & 0x3FU);
    }
    return value;
}

// MSVC spells std::array's iterator as a class type where libstdc++ and libc++ make
// it a raw pointer, so a std::find_if result over the table has no portable spelling.
inline const utf8_shape *utf8_shape_for(std::uint8_t lead) noexcept
{
    for(const utf8_shape &shape : utf8_shapes)
        if(lead >= shape.lead_low && lead <= shape.lead_high)
            return &shape;
    return nullptr;
}

inline std::optional<utf8_code_point> decode_utf8(std::string_view text,
                                                  std::size_t      offset) noexcept
{
    const auto              lead  = static_cast<std::uint8_t>(text[offset]);
    const utf8_shape *const shape = utf8_shape_for(lead);
    if(shape == nullptr || text.size() - offset <= shape->tail_count)
        return std::nullopt;
    const std::uint32_t leading = shape->tail_count == 0
            ? static_cast<std::uint32_t>(lead)
            : (lead & (0x3FU >> shape->tail_count));
    const auto          decoded = decode_utf8_tail(text, offset, *shape, leading);
    if(!decoded)
        return std::nullopt;
    return utf8_code_point{decoded.value(), shape->tail_count + 1};
}

inline bool is_valid_xml_name(std::string_view name) noexcept
{
    for(std::size_t offset = 0; offset < name.size();)
    {
        const auto decoded = decode_utf8(name, offset);
        if(!decoded)
            return false;
        if(!in_ranges(xml_name_start_ranges, decoded->value) &&
           (offset == 0 || !in_ranges(xml_name_extra_ranges, decoded->value)))
            return false;
        offset += decoded->length;
    }
    return !name.empty();
}

inline bool is_valid_xml_text(std::string_view text) noexcept
{
    for(std::size_t offset = 0; offset < text.size();)
    {
        const auto decoded = decode_utf8(text, offset);
        if(!decoded || !in_ranges(xml_char_ranges, decoded->value))
            return false;
        offset += decoded->length;
    }
    return true;
}

// A parser normalizes a literal carriage return in character data to a line feed,
// so a stored value carrying one cannot survive an emit/read round trip even
// though #xD is itself a legal Char.
inline bool is_round_trippable_xml_text(std::string_view text) noexcept
{
    return is_valid_xml_text(text) && text.find('\r') == std::string_view::npos;
}

inline error xml_grammar_error(std::string_view subject, std::string_view reason)
{
    return error{errc::malformed_source,
                 nucleus::format("xml emit: key '{}': {}", subject, reason)};
}

inline expected<void, error> validate_xml_name_segments(
        std::string_view subject, const std::vector<std::string> &segments)
{
    for(const std::string &segment : segments)
    {
        const std::string_view name = key_path::base_name(segment);
        if(!is_valid_xml_name(name))
            return unexpected(xml_grammar_error(subject, nucleus::format("segment '{}' is not a valid XML name", name)));
    }
    return {};
}

inline expected<void, error> validate_xml_stored_values(
        std::string_view subject, const std::vector<std::string> &values)
{
    for(const std::string &value : values)
        if(!is_round_trippable_xml_text(value))
            return unexpected(xml_grammar_error(subject, "value carries text XML cannot represent"));
    return {};
}

inline expected<void, error> validate_xml_annotations(
        std::string_view subject, const std::vector<std::string> &values)
{
    for(const std::string &value : values)
        if(!is_valid_xml_text(value))
            return unexpected(xml_grammar_error(subject, "an allowed value carries text XML cannot represent"));
    return {};
}

}

#endif
