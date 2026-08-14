#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_STORAGE_SHAPE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_STORAGE_SHAPE_H

#include "nucleus/keyspace/key_path.h"

#include "nucleus/error.h"
#include "nucleus/expected.h"

#include <span>

namespace nucleus {

expected<void, error> validate_storage_shape(std::span<const key_path> paths);

}

#endif
