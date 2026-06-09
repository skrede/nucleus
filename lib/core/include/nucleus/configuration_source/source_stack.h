#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_SOURCE_STACK_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_SOURCE_STACK_H

#include "nucleus/configuration_source/source_handle.h"

#include <span>
#include <vector>

namespace nucleus {

// Variadic front door that erases a pack of sources into an ordered vector.
// Order == precedence; a later-listed source overlays an earlier-listed one.
class source_stack
{
public:
    // Constructs a stack from any pack of concept-satisfying sources.
    template <source_satisfies... Ss>
    explicit source_stack(Ss... sources)
    {
        m_layers.reserve(sizeof...(Ss));
        (m_layers.emplace_back(source_handle(std::move(sources))), ...);
    }

    // Appends a pre-erased handle to the stack; later-appended handles have higher precedence.
    source_stack &push_back(source_handle h)
    {
        m_layers.push_back(std::move(h));
        return *this;
    }

    // Returns the ordered layers; index corresponds to precedence rank.
    [[nodiscard]] std::span<source_handle> layers() noexcept { return m_layers; }

    [[nodiscard]] std::size_t size() const noexcept { return m_layers.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_layers.empty(); }

private:
    // Order == precedence; the fold assigns ascending rank by index.
    std::vector<source_handle> m_layers;
};

}

#endif
