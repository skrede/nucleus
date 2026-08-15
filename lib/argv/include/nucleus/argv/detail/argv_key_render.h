#ifndef HPP_GUARD_NUCLEUS_ARGV_DETAIL_ARGV_KEY_RENDER_H
#define HPP_GUARD_NUCLEUS_ARGV_DETAIL_ARGV_KEY_RENDER_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <string_view>

namespace nucleus::argv::detail {

inline expected<std::string, error>
render_argv_key(const key_path &path, const cli_delimiter &delimiter)
{
    std::string rendered;
    for(const std::string &segment : path.segments())
    {
        const std::string_view base = key_path::base_name(segment);
        if(base.find(delimiter.str()) != std::string_view::npos)
            return unexpected(error{errc::malformed_source, nucleus::format("argv render: key '{}' contains delimiter '{}' inside segment '{}'", path.str(), delimiter.str(), base)});
        if(!rendered.empty())
            rendered.append(delimiter.str());
        rendered.append(base);
        if(key_path::is_indexed_segment(segment))
            rendered.append(delimiter.str())
                    .append(std::to_string(key_path::ordinal_of(segment)));
    }
    return rendered;
}

}

#endif
