#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_VALUE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_VALUE_H

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <string_view>

namespace nucleus {

// A view-or-owned keyspace value: a zero-copy source yields a view (string_view into
// a retained buffer), a transforming source yields an owned string. The discriminator
// makes the wrong state unrepresentable.
// Load-bearing view contract: the buffer a view points into must outlive every read;
// the producing source pins it (see config_source_batch). At the load boundary
// values are copied out and the buffers are dropped, leaving only owned values.
class value
{
public:
    // A view into a retained, externally-owned buffer. Zero-copy.
    static value view(std::string_view text) noexcept
    {
        return value(view_tag{}, text);
    }

    // An owned value the source built itself. Self-contained.
    static value owned(std::string text)
    {
        return value(owned_tag{}, std::move(text));
    }

    bool is_view() const noexcept { return m_data.index() == 0; }
    bool is_owned() const noexcept { return m_data.index() == 1; }

    // The text of the value regardless of ownership. For a view this aliases the
    // retained buffer and is only valid while that buffer is alive.
    std::string_view text() const noexcept
    {
        if(is_view())
            return std::get<0>(m_data);
        return std::get<1>(m_data);
    }

    // Severs any buffer dependency: an owned copy of the text. This is the copy-out
    // at the load boundary so the result outlives every source buffer.
    value to_owned() const { return owned(std::string(text())); }

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
