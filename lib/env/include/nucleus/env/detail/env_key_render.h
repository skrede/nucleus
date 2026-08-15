#ifndef HPP_GUARD_NUCLEUS_ENV_DETAIL_ENV_KEY_RENDER_H
#define HPP_GUARD_NUCLEUS_ENV_DETAIL_ENV_KEY_RENDER_H

#include "nucleus/error.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include <string>

namespace nucleus::env::detail {

inline expected<std::string, error> render_env_key(const key_path &path)
{
    return path.str();
}

}

#endif
