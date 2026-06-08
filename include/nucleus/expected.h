#ifndef HPP_GUARD_NUCLEUS_EXPECTED_H
#define HPP_GUARD_NUCLEUS_EXPECTED_H

#include "nucleus/detail/expected.h"

// Public seam for the fallible-return vocabulary. A future C++23 migration points
// these aliases at std::expected / std::unexpected and edits nothing else.
namespace nucleus {

using detail::expected;
using detail::unexpected;
using detail::unexpect_t;
using detail::unexpect;

}

#endif
