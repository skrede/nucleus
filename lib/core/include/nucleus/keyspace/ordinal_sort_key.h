#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_ORDINAL_SORT_KEY_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_ORDINAL_SORT_KEY_H

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <string_view>

namespace nucleus {

// Produces a sort key for a config map key so that numeric ordinals in indexed
// segments compare by value, not lexicographically. Each segment yields a
// (base_name, ordinal) pair; non-indexed segments use ordinal = 0.
// "cluster/node[10]/port" < "cluster/node[2]/port" lexicographically, but this
// key correctly orders node[2] before node[10].
inline std::vector<std::pair<std::string, std::uint64_t>>
ordinal_sort_key(const std::string &key)
{
    std::vector<std::pair<std::string, std::uint64_t>> parts;
    std::size_t start = 0;
    for(std::size_t i = 0; i <= key.size(); ++i)
    {
        if(i == key.size() || key[i] == key_path::separator)
        {
            std::string_view const seg(key.data() + start, i - start);
            if(key_path::is_indexed_segment(seg))
                parts.emplace_back(std::string(key_path::base_name(seg)),
                                   key_path::ordinal_of(seg));
            else
                parts.emplace_back(std::string(seg), std::uint64_t{0});
            start = i + 1;
        }
    }
    return parts;
}

}

#endif
