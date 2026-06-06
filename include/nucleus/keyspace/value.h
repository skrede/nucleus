#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_VALUE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_VALUE_H

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <string_view>

namespace nucleus {

// A view-or-owned keyspace value.
//
// A zero-copy source yields a view: a string_view into a retained buffer (the
// raw bytes of a non-document source, or a parser's own document arena). A
// transforming source yields an owned value: a string it built itself. The
// engine never mandates ownership -- the source decides, and the discriminator
// here makes the wrong state unrepresentable: a view always carries a stable
// string_view; an owned value always carries the std::string that backs it.
//
// The lifetime contract for the view alternative is external and load-bearing:
// the buffer the view points into must outlive every read of that view. A
// source that produces views attaches the owning handle that pins that buffer
// (see source_batch). At the resolve boundary, typed values are copied out and
// the buffers are dropped -- after which only owned values survive.
class value
{
public:
    // A view into a retained, externally-owned buffer. Zero-copy.
    [[nodiscard]] static value view(std::string_view text) noexcept
    {
        return value(view_tag{}, text);
    }

    // An owned value the source built itself. Self-contained.
    [[nodiscard]] static value owned(std::string text)
    {
        return value(owned_tag{}, std::move(text));
    }

    [[nodiscard]] bool is_view() const noexcept { return m_data.index() == 0; }
    [[nodiscard]] bool is_owned() const noexcept { return m_data.index() == 1; }

    // The text of the value regardless of ownership. For a view this aliases the
    // retained buffer and is only valid while that buffer is alive.
    [[nodiscard]] std::string_view text() const noexcept
    {
        if(is_view())
            return std::get<0>(m_data);
        return std::get<1>(m_data);
    }

    // Severs any buffer dependency: returns an owned value holding a copy of the
    // text. This is the copy-out performed at the resolve boundary so the result
    // outlives every source buffer.
    [[nodiscard]] value to_owned() const { return owned(std::string(text())); }

private:
    struct view_tag {};
    struct owned_tag {};

    value(view_tag, std::string_view text)
        : m_data(std::in_place_index<0>, text)
    {
    }

    value(owned_tag, std::string text)
        : m_data(std::in_place_index<1>, std::move(text))
    {
    }

    std::variant<std::string_view, std::string> m_data;
};

}

#endif
