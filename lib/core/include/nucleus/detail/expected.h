#ifndef HPP_GUARD_NUCLEUS_DETAIL_EXPECTED_H
#define HPP_GUARD_NUCLEUS_DETAIL_EXPECTED_H

#include <utility>
#include <variant>
#include <functional>
#include <type_traits>

namespace nucleus::detail {

// Tag selecting the error in-place constructor of expected, mirroring std::unexpect.
struct unexpect_t
{
    explicit unexpect_t() = default;
};

inline constexpr unexpect_t unexpect{};

// Error wrapper mirroring std::unexpected: carries an error value distinct from
// the expected value alternative even when the value and error types coincide.
template <typename E>
class unexpected
{
public:
    constexpr unexpected(const unexpected &) = default;
    constexpr unexpected(unexpected &&) = default;
    constexpr unexpected &operator=(const unexpected &) = default;
    constexpr unexpected &operator=(unexpected &&) = default;

    template <typename Err = E,
              typename = std::enable_if_t<
                  !std::is_same_v<std::remove_cvref_t<Err>, unexpected> &&
                  !std::is_same_v<std::remove_cvref_t<Err>, std::in_place_t> &&
                  std::is_constructible_v<E, Err>>>
    constexpr explicit unexpected(Err &&error) : m_error(std::forward<Err>(error)) {}

    template <typename... Args>
    constexpr explicit unexpected(std::in_place_t, Args &&...args)
        : m_error(std::forward<Args>(args)...)
    {
    }

    [[nodiscard]] constexpr E &error() & noexcept { return m_error; }
    [[nodiscard]] constexpr const E &error() const & noexcept { return m_error; }
    [[nodiscard]] constexpr E &&error() && noexcept { return std::move(m_error); }
    [[nodiscard]] constexpr const E &&error() const && noexcept { return std::move(m_error); }

    constexpr void swap(unexpected &other) noexcept(std::is_nothrow_swappable_v<E>)
    {
        using std::swap;
        swap(m_error, other.m_error);
    }

    template <typename E2>
    [[nodiscard]] friend constexpr bool operator==(const unexpected &lhs, const unexpected<E2> &rhs)
    {
        return lhs.error() == rhs.error();
    }

private:
    E m_error;
};

template <typename E>
unexpected(E) -> unexpected<E>;

template <typename T>
struct is_unexpected : std::false_type {};
template <typename E>
struct is_unexpected<unexpected<E>> : std::true_type {};

template <typename T, typename E>
class expected;

template <typename T>
struct is_expected : std::false_type {};
template <typename T, typename E>
struct is_expected<expected<T, E>> : std::true_type {};

// Variant-backed value-or-error mirroring std::expected under the C++20 contract:
// holds exactly one of a value (T) or an error (E). Index 0 is the value.
template <typename T, typename E>
class expected
{
public:
    using value_type = T;
    using error_type = E;
    using unexpected_type = unexpected<E>;

    template <typename U>
    using rebind = expected<U, error_type>;

    constexpr expected()
        requires std::is_default_constructible_v<T>
        : m_data(std::in_place_index<0>)
    {
    }

    constexpr expected(const expected &) = default;
    constexpr expected(expected &&) = default;
    constexpr expected &operator=(const expected &) = default;
    constexpr expected &operator=(expected &&) = default;

    template <typename U = T,
              typename = std::enable_if_t<
                  !std::is_same_v<std::remove_cvref_t<U>, expected> &&
                  !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
                  !std::is_same_v<std::remove_cvref_t<U>, unexpect_t> &&
                  !is_unexpected<std::remove_cvref_t<U>>::value &&
                  std::is_constructible_v<T, U>>>
    constexpr explicit(!std::is_convertible_v<U, T>) expected(U &&value)
        : m_data(std::in_place_index<0>, std::forward<U>(value))
    {
    }

    template <typename G,
              typename = std::enable_if_t<std::is_constructible_v<E, const G &>>>
    constexpr explicit(!std::is_convertible_v<const G &, E>) expected(const unexpected<G> &error)
        : m_data(std::in_place_index<1>, error.error())
    {
    }

    template <typename G,
              typename = std::enable_if_t<std::is_constructible_v<E, G>>>
    constexpr explicit(!std::is_convertible_v<G, E>) expected(unexpected<G> &&error)
        : m_data(std::in_place_index<1>, std::move(error).error())
    {
    }

    template <typename... Args>
    constexpr explicit expected(std::in_place_t, Args &&...args)
        : m_data(std::in_place_index<0>, std::forward<Args>(args)...)
    {
    }

    template <typename... Args>
    constexpr explicit expected(unexpect_t, Args &&...args)
        : m_data(std::in_place_index<1>, std::forward<Args>(args)...)
    {
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return m_data.index() == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr T *operator->() noexcept { return std::addressof(std::get<0>(m_data)); }
    [[nodiscard]] constexpr const T *operator->() const noexcept { return std::addressof(std::get<0>(m_data)); }

    [[nodiscard]] constexpr T &operator*() & noexcept { return std::get<0>(m_data); }
    [[nodiscard]] constexpr const T &operator*() const & noexcept { return std::get<0>(m_data); }
    [[nodiscard]] constexpr T &&operator*() && noexcept { return std::get<0>(std::move(m_data)); }
    [[nodiscard]] constexpr const T &&operator*() const && noexcept { return std::get<0>(std::move(m_data)); }

    [[nodiscard]] constexpr T &value() & { return std::get<0>(m_data); }
    [[nodiscard]] constexpr const T &value() const & { return std::get<0>(m_data); }
    [[nodiscard]] constexpr T &&value() && { return std::get<0>(std::move(m_data)); }
    [[nodiscard]] constexpr const T &&value() const && { return std::get<0>(std::move(m_data)); }

    [[nodiscard]] constexpr E &error() & { return std::get<1>(m_data); }
    [[nodiscard]] constexpr const E &error() const & { return std::get<1>(m_data); }
    [[nodiscard]] constexpr E &&error() && { return std::get<1>(std::move(m_data)); }
    [[nodiscard]] constexpr const E &&error() const && { return std::get<1>(std::move(m_data)); }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&fallback) const &
    {
        return has_value() ? std::get<0>(m_data) : static_cast<T>(std::forward<U>(fallback));
    }
    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&fallback) &&
    {
        return has_value() ? std::get<0>(std::move(m_data)) : static_cast<T>(std::forward<U>(fallback));
    }

    template <typename G = E>
    [[nodiscard]] constexpr E error_or(G &&fallback) const &
    {
        return has_value() ? static_cast<E>(std::forward<G>(fallback)) : std::get<1>(m_data);
    }
    template <typename G = E>
    [[nodiscard]] constexpr E error_or(G &&fallback) &&
    {
        return has_value() ? static_cast<E>(std::forward<G>(fallback)) : std::get<1>(std::move(m_data));
    }

    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) &
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, T &>>;
        if (has_value()) return std::invoke(std::forward<F>(f), std::get<0>(m_data));
        return U(unexpect, std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) const &
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T &>>;
        if (has_value()) return std::invoke(std::forward<F>(f), std::get<0>(m_data));
        return U(unexpect, std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) &&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, T &&>>;
        if (has_value()) return std::invoke(std::forward<F>(f), std::get<0>(std::move(m_data)));
        return U(unexpect, std::get<1>(std::move(m_data)));
    }
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) const &&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T &&>>;
        if (has_value()) return std::invoke(std::forward<F>(f), std::get<0>(std::move(m_data)));
        return U(unexpect, std::get<1>(std::move(m_data)));
    }

    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) &
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E &>>;
        if (has_value()) return G(std::in_place, std::get<0>(m_data));
        return std::invoke(std::forward<F>(f), std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) const &
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E &>>;
        if (has_value()) return G(std::in_place, std::get<0>(m_data));
        return std::invoke(std::forward<F>(f), std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) &&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E &&>>;
        if (has_value()) return G(std::in_place, std::get<0>(std::move(m_data)));
        return std::invoke(std::forward<F>(f), std::get<1>(std::move(m_data)));
    }
    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) const &&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E &&>>;
        if (has_value()) return G(std::in_place, std::get<0>(std::move(m_data)));
        return std::invoke(std::forward<F>(f), std::get<1>(std::move(m_data)));
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) &
    {
        return transform_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) const &
    {
        return transform_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) &&
    {
        return transform_impl(std::forward<F>(f), std::move(*this));
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) const &&
    {
        return transform_impl(std::forward<F>(f), std::move(*this));
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) &
    {
        return transform_error_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) const &
    {
        return transform_error_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) &&
    {
        return transform_error_impl(std::forward<F>(f), std::move(*this));
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) const &&
    {
        return transform_error_impl(std::forward<F>(f), std::move(*this));
    }

    template <typename T2, typename E2>
    [[nodiscard]] friend constexpr bool operator==(const expected &lhs, const expected<T2, E2> &rhs)
    {
        if (lhs.has_value() != rhs.has_value()) return false;
        return lhs.has_value() ? static_cast<bool>(*lhs == *rhs)
                               : static_cast<bool>(lhs.error() == rhs.error());
    }
    template <typename E2>
    [[nodiscard]] friend constexpr bool operator==(const expected &lhs, const unexpected<E2> &rhs)
    {
        return !lhs.has_value() && static_cast<bool>(lhs.error() == rhs.error());
    }

private:
    // Forwards the stored value with Self's value category so a move-only T flows
    // through transform without a copy; wraps the result back into expected<U, E>.
    template <typename F, typename Self>
    static constexpr auto transform_impl(F &&f, Self &&self)
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>>;
        if constexpr (std::is_void_v<U>)
        {
            if (self.has_value())
            {
                std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
                return expected<void, E>();
            }
            return expected<void, E>(unexpect, std::forward<Self>(self).error());
        }
        else
        {
            if (self.has_value())
                return expected<U, E>(std::invoke(std::forward<F>(f), std::forward<Self>(self).value()));
            return expected<U, E>(unexpect, std::forward<Self>(self).error());
        }
    }

    template <typename F, typename Self>
    static constexpr auto transform_error_impl(F &&f, Self &&self)
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        if (self.has_value())
            return expected<T, G>(std::in_place, std::forward<Self>(self).value());
        return expected<T, G>(unexpect, std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
    }

    std::variant<T, E> m_data;
};

// expected<void, E>: no value storage; a chain may terminate in a void-returning callable.
template <typename E>
class expected<void, E>
{
public:
    using value_type = void;
    using error_type = E;
    using unexpected_type = unexpected<E>;

    template <typename U>
    using rebind = expected<U, error_type>;

    constexpr expected() noexcept : m_data(std::in_place_index<0>) {}

    constexpr expected(const expected &) = default;
    constexpr expected(expected &&) = default;
    constexpr expected &operator=(const expected &) = default;
    constexpr expected &operator=(expected &&) = default;

    constexpr explicit expected(std::in_place_t) noexcept : m_data(std::in_place_index<0>) {}

    template <typename G,
              typename = std::enable_if_t<std::is_constructible_v<E, const G &>>>
    constexpr explicit(!std::is_convertible_v<const G &, E>) expected(const unexpected<G> &error)
        : m_data(std::in_place_index<1>, error.error())
    {
    }

    template <typename G,
              typename = std::enable_if_t<std::is_constructible_v<E, G>>>
    constexpr explicit(!std::is_convertible_v<G, E>) expected(unexpected<G> &&error)
        : m_data(std::in_place_index<1>, std::move(error).error())
    {
    }

    template <typename... Args>
    constexpr explicit expected(unexpect_t, Args &&...args)
        : m_data(std::in_place_index<1>, std::forward<Args>(args)...)
    {
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return m_data.index() == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr void operator*() const noexcept {}
    constexpr void value() const & { (void)std::get<0>(m_data); }
    constexpr void value() && { (void)std::get<0>(m_data); }
    constexpr void emplace() noexcept { m_data.template emplace<0>(); }

    [[nodiscard]] constexpr E &error() & { return std::get<1>(m_data); }
    [[nodiscard]] constexpr const E &error() const & { return std::get<1>(m_data); }
    [[nodiscard]] constexpr E &&error() && { return std::get<1>(std::move(m_data)); }
    [[nodiscard]] constexpr const E &&error() const && { return std::get<1>(std::move(m_data)); }

    template <typename G = E>
    [[nodiscard]] constexpr E error_or(G &&fallback) const &
    {
        return has_value() ? static_cast<E>(std::forward<G>(fallback)) : std::get<1>(m_data);
    }
    template <typename G = E>
    [[nodiscard]] constexpr E error_or(G &&fallback) &&
    {
        return has_value() ? static_cast<E>(std::forward<G>(fallback)) : std::get<1>(std::move(m_data));
    }

    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) &
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (has_value()) return std::invoke(std::forward<F>(f));
        return U(unexpect, std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) const &
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (has_value()) return std::invoke(std::forward<F>(f));
        return U(unexpect, std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) &&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (has_value()) return std::invoke(std::forward<F>(f));
        return U(unexpect, std::get<1>(std::move(m_data)));
    }
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F &&f) const &&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (has_value()) return std::invoke(std::forward<F>(f));
        return U(unexpect, std::get<1>(std::move(m_data)));
    }

    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) &
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E &>>;
        if (has_value()) return G();
        return std::invoke(std::forward<F>(f), std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) const &
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E &>>;
        if (has_value()) return G();
        return std::invoke(std::forward<F>(f), std::get<1>(m_data));
    }
    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) &&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E &&>>;
        if (has_value()) return G();
        return std::invoke(std::forward<F>(f), std::get<1>(std::move(m_data)));
    }
    template <typename F>
    [[nodiscard]] constexpr auto or_else(F &&f) const &&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E &&>>;
        if (has_value()) return G();
        return std::invoke(std::forward<F>(f), std::get<1>(std::move(m_data)));
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) &
    {
        return transform_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) const &
    {
        return transform_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) &&
    {
        return transform_impl(std::forward<F>(f), std::move(*this));
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform(F &&f) const &&
    {
        return transform_impl(std::forward<F>(f), std::move(*this));
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) &
    {
        return transform_error_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) const &
    {
        return transform_error_impl(std::forward<F>(f), *this);
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) &&
    {
        return transform_error_impl(std::forward<F>(f), std::move(*this));
    }
    template <typename F>
    [[nodiscard]] constexpr auto transform_error(F &&f) const &&
    {
        return transform_error_impl(std::forward<F>(f), std::move(*this));
    }

    template <typename E2>
    [[nodiscard]] friend constexpr bool operator==(const expected &lhs, const expected<void, E2> &rhs)
    {
        if (lhs.has_value() != rhs.has_value()) return false;
        return lhs.has_value() ? true : static_cast<bool>(lhs.error() == rhs.error());
    }
    template <typename E2>
    [[nodiscard]] friend constexpr bool operator==(const expected &lhs, const unexpected<E2> &rhs)
    {
        return !lhs.has_value() && static_cast<bool>(lhs.error() == rhs.error());
    }

private:
    template <typename F, typename Self>
    static constexpr auto transform_impl(F &&f, Self &&self)
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if constexpr (std::is_void_v<U>)
        {
            if (self.has_value())
            {
                std::invoke(std::forward<F>(f));
                return expected<void, E>();
            }
            return expected<void, E>(unexpect, std::forward<Self>(self).error());
        }
        else
        {
            if (self.has_value()) return expected<U, E>(std::invoke(std::forward<F>(f)));
            return expected<U, E>(unexpect, std::forward<Self>(self).error());
        }
    }

    template <typename F, typename Self>
    static constexpr auto transform_error_impl(F &&f, Self &&self)
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        if (self.has_value()) return expected<void, G>();
        return expected<void, G>(unexpect, std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
    }

    std::variant<std::monostate, E> m_data;
};

}

#endif
