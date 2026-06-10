#ifndef HPP_GUARD_NUCLEUS_CAPABILITY_H
#define HPP_GUARD_NUCLEUS_CAPABILITY_H

#include <array>
#include <cstdint>
#include <string_view>

namespace nucleus {

// The affordances a source can or cannot represent. The set is deliberately
// format-neutral: it names structural capabilities, never a format. Each source
// declares which of these it provides; the engine intersects a schema's
// requirements with a source's capabilities to decide, per feature, whether it
// is honored, degraded, or refused. (See feature_gate.)
enum class capability : std::uint8_t
{
    // Hierarchical structure: a value can nest under another (a/b/c). A
    // map-shaped source (env) cannot; a path-addressed one (argv, runtime) can.
    nesting,
    // Two entries may share a key path within the same scope (repeated keys).
    // A map-shaped source collapses repeats to last-wins.
    duplicate_keys,
    // The source distinguishes typed scalars (int/bool/...) rather than handing
    // everything back as text.
    typed_scalars,
    // The source preserves comments alongside values.
    comments,
    // The source preserves the order in which entries appeared.
    ordering,
};

[[nodiscard]] constexpr std::string_view to_string(capability cap) noexcept
{
    switch(cap)
    {
        case capability::nesting:        return "nesting";
        case capability::duplicate_keys: return "duplicate_keys";
        case capability::typed_scalars:  return "typed_scalars";
        case capability::comments:       return "comments";
        case capability::ordering:       return "ordering";
    }
    return "unknown";
}

// A source's declared affordances. It is a small fixed-size bit set keyed by the
// capability enum -- declarative and trivially comparable, so feature gating is
// data, not per-format special-casing. A descriptor that claims everything is a
// red flag (it would never exercise degradation); a capability-poor source like
// env declares an honestly restrictive descriptor.
class capability_descriptor
{
public:
    constexpr capability_descriptor() = default;

    // Builds a descriptor from a list of supported capabilities; anything not
    // listed is unsupported.
    constexpr capability_descriptor(std::initializer_list<capability> supported)
    {
        for(capability cap : supported)
            m_flags[index(cap)] = true;
    }

    [[nodiscard]] constexpr bool supports(capability cap) const noexcept
    {
        return m_flags[index(cap)];
    }

    [[nodiscard]] constexpr capability_descriptor &with(capability cap) noexcept
    {
        m_flags[index(cap)] = true;
        return *this;
    }

private:
    static constexpr std::size_t count = 5;

    static constexpr std::size_t index(capability cap) noexcept
    {
        return static_cast<std::size_t>(cap);
    }

    std::array<bool, count> m_flags{};
};

}

#endif
