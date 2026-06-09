#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CLI_FLAG_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CLI_FLAG_H

#include "nucleus/keyspace/key_path.h"

#include <string>

namespace nucleus {

// The inverse projection: a keyspace path back to its canonical CLI flag. Because
// segments never contain hyphens, this is a total, lossless inverse of the
// segmentation -- the bijection made explicit and the basis for the schema-
// projected flag surface and tab completion. Core's bijection authority.
[[nodiscard]] inline std::string flag_of(const key_path &path)
{
    std::string flag = "--";
    const auto &segments = path.segments();
    for(std::size_t i = 0; i < segments.size(); ++i)
    {
        if(i != 0)
            flag.push_back('-');
        flag.append(segments[i]);
    }
    return flag;
}

}

#endif
