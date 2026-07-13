#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_SUBSTITUTION_BUDGET_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_SUBSTITUTION_BUDGET_H

#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/tokenizer/resolve_error.h"

#include <cstddef>

namespace nucleus {

// Default ceiling on total pass-1 substitutions in one load. Pass-2 keeps its
// larger default_reference_budget; the two defaults deliberately diverge.
inline constexpr std::size_t default_expansion_budget = 2500;

// A running substitution-count budget shared across one whole expansion pass.
// charge() is called once per substitution; when the running count passes the
// cap it fails loudly with budget_exceeded, bounding fanout amplification that a
// depth cap alone cannot stop. Borrowed by reference so every value in a load
// shares one count (Option B, per-load scope).
struct substitution_budget
{
    std::size_t count = 0;
    std::size_t cap   = 0;

    substitution_budget() = default;
    explicit substitution_budget(std::size_t budget_cap) noexcept : cap(budget_cap) {}

    expected<void, resolve_error> charge()
    {
        ++count;
        if(count > cap)
            return unexpected(resolve_error(resolve_errc::budget_exceeded,
                nucleus::format("substitution budget ({}) exceeded", cap)));
        return {};
    }
};

}

#endif
