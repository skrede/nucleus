#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CONVERTER_REGISTRY_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CONVERTER_REGISTRY_H

#include "nucleus/expected.h"

#include <any>
#include <map>
#include <string>
#include <cstddef>
#include <typeindex>
#include <functional>
#include <string_view>
#include <type_traits>

namespace nucleus {

// One of the four flat sibling registries. Maps a std::type_index to the value
// converter for that type -- the resolve-time fallback for a typed element that
// declares a type but carries no per-element converter. Holds NO reference/
// pointer/handle to any other registry; siblings are passed as parameters via the
// transient resolution context, never stored. See schema_registry for the
// invariant note.
//
// Value-copyable: the std::map of converters copies its entries, so a member-wise
// copy is a deep copy. This is REQUIRED so the sealed space's expand() can clone
// the registry without any shared state.
class converter_registry
{
public:
    using converter = std::function<expected<std::any, std::string>(std::string_view)>;

    converter_registry() = default;

    // Registers (or replaces) the converter for `id`. A later registration of the
    // same type shadows the earlier one.
    void add(std::type_index id, converter fn)
    {
        m_converters.insert_or_assign(id, std::move(fn));
    }

    // Registers the converter for T, keyed identically to the typed_element model.
    template<typename T>
    void set(converter fn)
    {
        add(std::type_index(typeid(std::remove_cvref_t<T>)), std::move(fn));
    }

    // The stored converter for `id`, or nullptr when none is registered.
    const converter *find(std::type_index id) const
    {
        auto it = m_converters.find(id);
        return it == m_converters.end() ? nullptr : &it->second;
    }

    std::size_t size() const noexcept { return m_converters.size(); }

private:
    std::map<std::type_index, converter> m_converters;
};

}

#endif
