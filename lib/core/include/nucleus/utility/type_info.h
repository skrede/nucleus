#ifndef HPP_GUARD_NUCLEUS_UTILITY_TYPE_INFO_H
#define HPP_GUARD_NUCLEUS_UTILITY_TYPE_INFO_H

#include <cstddef>
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <type_traits>

namespace nucleus::detail {

// Readable type name parsed out of the compiler's own signature spelling. The
// name is whatever the toolchain renders (e.g. std::__cxx11::basic_string<char>);
// it is for diagnostics, not a stable wire identity.
template <typename T>
constexpr std::string_view type_name()
{
#if defined(__GNUC__) || defined(__clang__)
    constexpr std::string_view signature = __PRETTY_FUNCTION__;
    constexpr std::string_view marker = "T = ";
    const std::size_t begin = signature.find(marker) + marker.size();
    const std::size_t end = signature.find_first_of(";]", begin);
    return signature.substr(begin, end - begin);
#elif defined(_MSC_VER)
    constexpr std::string_view signature = __FUNCSIG__;
    const std::size_t begin = signature.find("type_name<") + 10;
    const std::size_t end = signature.rfind(">(void)");
    std::string_view name = signature.substr(begin, end - begin);
    for (std::string_view tag : {"class ", "struct ", "enum "})
        while (name.starts_with(tag)) name.remove_prefix(tag.size());
    return name;
#else
#error "type_name: unsupported compiler"
#endif
}

template <typename T>
constexpr std::size_t size_or_zero()
{
    if constexpr (std::is_void_v<T> || std::is_abstract_v<T>)
        return 0;
    else
        return sizeof(T);
}

// Process-local type descriptor: std::type_index identity (the same key get_as<T>
// matches on) plus a readable name and a few traits for diagnostics.
struct type_info
{
    std::string_view name;
    std::type_index  id;
    std::size_t      size;
    bool             is_fundamental;
    bool             is_enum;
};

template <typename T>
const type_info &make_type_info()
{
    using bare = std::remove_cvref_t<T>;
    static const type_info info{
        type_name<bare>(),
        std::type_index(typeid(bare)),
        size_or_zero<bare>(),
        std::is_fundamental_v<bare>,
        std::is_enum_v<bare>,
    };
    return info;
}

}

namespace nucleus {
using detail::type_info;
using detail::make_type_info;
}

#endif
