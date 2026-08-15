#ifndef HPP_GUARD_NUCLEUS_DETAIL_FLAT_ANCHOR_H
#define HPP_GUARD_NUCLEUS_DETAIL_FLAT_ANCHOR_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <string_view>

namespace nucleus::detail {

inline bool flat_anchor_segment_matches(std::string_view anchor,
                                        std::string_view concrete) noexcept
{
    if(key_path::base_name(anchor) != key_path::base_name(concrete))
        return false;
    if(!key_path::is_indexed_segment(anchor))
        return true;
    return key_path::is_indexed_segment(concrete) && key_path::ordinal_of(anchor) == key_path::ordinal_of(concrete);
}

inline bool flat_anchor_matches(const key_path &path,
                                const key_path &anchor) noexcept
{
    if(anchor.size() > path.size())
        return false;
    for(std::size_t i = 0; i < anchor.size(); ++i)
        if(!flat_anchor_segment_matches(anchor.segments()[i], path.segments()[i]))
            return false;
    return true;
}

inline key_path flat_relative_path(const key_path &path, const key_path &anchor)
{
    std::vector<std::string> segments;
    for(std::size_t i = 0; i < anchor.size(); ++i)
        if(!key_path::is_indexed_segment(anchor.segments()[i]) && key_path::is_indexed_segment(path.segments()[i]))
            segments.push_back(std::to_string(
                    key_path::ordinal_of(path.segments()[i])));
    segments.insert(segments.end(), path.segments().begin() + static_cast<std::ptrdiff_t>(anchor.size()), path.segments().end());
    return key_path(std::move(segments));
}

inline expected<std::optional<key_path>, error>
select_flat_path(const key_path &path, const key_path &anchor)
{
    if(anchor.empty())
        return std::optional<key_path>{path};
    if(!flat_anchor_matches(path, anchor))
        return std::optional<key_path>{std::nullopt};
    if(path.size() == anchor.size())
        return unexpected(error{errc::malformed_source, nucleus::format("flat render: anchor '{}' selects scalar key '{}'; an anchor must "
                                                                        "name a structural root",
                                                                        anchor.str(), path.str())});
    return std::optional<key_path>{flat_relative_path(path, anchor)};
}

}

#endif
