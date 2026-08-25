#ifndef HPP_GUARD_NUCLEUS_FORMAT_H
#define HPP_GUARD_NUCLEUS_FORMAT_H

#include "nucleus/detail/format_backend.h"

// Diagnostic / sink vocabulary, over std::format where it was available and the {fmt} reference
// implementation where it was not. Which one is settled at nucleus's configure time and carried
// in the generated header above: the consuming translation unit's own standard library is not the
// one nucleus's archive was compiled against, so it cannot answer the question. The seam's
// signature is the same under either backend.

#if NUCLEUS_USE_STD_FORMAT

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

// fmt/format.h, not fmt/core.h: since fmt 11 the core header aliases fmt/base.h,
// which no longer declares fmt::format itself.
#include <fmt/format.h>

namespace nucleus {

template <typename... Args>
std::string format(fmt::format_string<Args...> spec, Args &&...args)
{
    return fmt::format(spec, std::forward<Args>(args)...);
}

}

#endif

#endif
