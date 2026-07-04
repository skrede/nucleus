#ifndef HPP_GUARD_NUCLEUS_FORMAT_H
#define HPP_GUARD_NUCLEUS_FORMAT_H

#include <version>

// Diagnostic / sink vocabulary. std::format is the interface vocabulary, but it
// is not yet portable enough to depend on unconditionally (Apple Clang / libc++
// has historically lagged). When the standard library advertises std::format
// via __cpp_lib_format we use it directly; otherwise we fall back to the {fmt}
// library, which is the reference implementation and API-compatible. The seam's
// signature never changes regardless of which backend is active.

#ifdef __cpp_lib_format

#include <string>
#include <format>

namespace nucleus {

template <typename... Args>
std::string format(std::format_string<Args...> spec, Args &&...args)
{
    return std::format(spec, std::forward<Args>(args)...);
}

}

#else

#include <string>

#include <fmt/core.h>

namespace nucleus {

template <typename... Args>
std::string format(fmt::format_string<Args...> spec, Args &&...args)
{
    return fmt::format(spec, std::forward<Args>(args)...);
}

}

#endif

#endif
