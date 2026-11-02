#ifndef HPP_GUARD_NUCLEUS_EXPECTED_H
#define HPP_GUARD_NUCLEUS_EXPECTED_H

#include "nucleus/detail/expected.h"

// These aliases move to std::expected / std::unexpected only when the language floor becomes
// C++23, and that move is a breaking release: it changes the public type and what a misused accessor throws.
namespace nucleus {

using detail::expected;
using detail::unexpected;
using detail::unexpect_t;
using detail::unexpect;

}

#endif
