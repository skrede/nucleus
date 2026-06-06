#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_EXPANSION_GUARD_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_EXPANSION_GUARD_H

#include "nucleus/format.h"
#include "nucleus/result.h"

#include "nucleus/tokenizer/resolve_error.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus {

// Default ceiling on nested ${a${b}} expansion. Recursive-to-fixpoint resolution
// would otherwise recurse forever on a pathological input; a cap turns that into
// a loud, named error. Generous enough that legitimate nesting never trips it.
inline constexpr std::size_t default_expansion_depth_cap = 16;

// Tracks the live ${...} expansion as a stack and converts the two halting
// conditions into named errors instead of unbounded recursion:
//
//   * depth -- nesting deeper than the cap yields resolve_errc::depth_exceeded.
//   * cycle -- re-entering a token label already on the active chain yields
//     resolve_errc::cyclic_reference, with the ordered chain (a -> b -> a)
//     embedded in the message. A self-reference (a -> a) is the degenerate
//     single-element cycle and reports the same way.
//
// enter() returns a guard that pops the label on destruction (including unwind),
// so sibling tokens and a legitimate re-use after a token returns stay clear.
class expansion_guard
{
public:
    class scope
    {
    public:
        scope() = default;
        explicit scope(expansion_guard *owner) noexcept : m_owner(owner) {}

        scope(const scope &) = delete;
        scope &operator=(const scope &) = delete;

        scope(scope &&other) noexcept : m_owner(other.m_owner) { other.m_owner = nullptr; }
        scope &operator=(scope &&other) noexcept
        {
            if(this != &other)
            {
                release();
                m_owner = other.m_owner;
                other.m_owner = nullptr;
            }
            return *this;
        }

        ~scope() { release(); }

    private:
        void release() noexcept
        {
            if(m_owner)
            {
                if(!m_owner->m_chain.empty())
                    m_owner->m_chain.pop_back();
                m_owner = nullptr;
            }
        }

        expansion_guard *m_owner = nullptr;
    };

    explicit expansion_guard(std::size_t cap = default_expansion_depth_cap) noexcept
        : m_cap(cap)
    {
    }

    // Pushes `label` onto the active chain. Fails with cyclic_reference when the
    // label is already live (naming the ordered cycle) or depth_exceeded when the
    // chain would grow past the cap. On success returns a popping guard.
    [[nodiscard]] result<scope, resolve_error> enter(std::string label)
    {
        auto first = std::find(m_chain.begin(), m_chain.end(), label);
        if(first != m_chain.end())
        {
            std::string chain;
            for(auto it = first; it != m_chain.end(); ++it)
                chain += *it + " -> ";
            chain += label;
            return fail(resolve_error(resolve_errc::cyclic_reference,
                                      nucleus::format("cyclic reference: {}", chain)));
        }
        if(m_chain.size() >= m_cap)
            return fail(resolve_error(resolve_errc::depth_exceeded,
                                      nucleus::format("token expansion depth {} exceeded", m_cap)));
        m_chain.push_back(std::move(label));
        return scope(this);
    }

    [[nodiscard]] std::size_t depth() const noexcept { return m_chain.size(); }

private:
    std::vector<std::string> m_chain;
    std::size_t m_cap;
};

}

#endif
