#ifndef HPP_GUARD_NUCLEUS_RESULT_H
#define HPP_GUARD_NUCLEUS_RESULT_H

#include <utility>
#include <variant>
#include <type_traits>

namespace nucleus {

// Tag wrapper that disambiguates the error alternative of result<T, E> from its
// value alternative -- necessary when T and E are the same or convertible types.
template <typename E>
struct failure
{
    E error;
};

template <typename E>
failure(E) -> failure<E>;

template <typename E>
[[nodiscard]] constexpr failure<std::decay_t<E>> fail(E &&error)
{
    return failure<std::decay_t<E>>{std::forward<E>(error)};
}

// In-house result<T, E> used across public headers. std::expected is C++23 and
// outside the C++20 contract, so this is the engine's fallible-return vocabulary.
// Holds exactly one of a value (T) or an error (E).
template <typename T, typename E>
class result
{
public:
    using value_type = T;
    using error_type = E;

    constexpr result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}

    constexpr result(failure<E> error)
        : m_data(std::in_place_index<1>, std::move(error.error))
    {
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return m_data.index() == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr T &value() & { return std::get<0>(m_data); }
    [[nodiscard]] constexpr const T &value() const & { return std::get<0>(m_data); }
    [[nodiscard]] constexpr T &&value() && { return std::get<0>(std::move(m_data)); }

    [[nodiscard]] constexpr E &error() & { return std::get<1>(m_data); }
    [[nodiscard]] constexpr const E &error() const & { return std::get<1>(m_data); }
    [[nodiscard]] constexpr E &&error() && { return std::get<1>(std::move(m_data)); }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&fallback) const &
    {
        return has_value() ? std::get<0>(m_data) : static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, E> m_data;
};

}

#endif
