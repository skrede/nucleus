#ifndef HPP_GUARD_NUCLEUS_CONFIG_SOURCE_ARGV_KEY_RECOGNIZER_H
#define HPP_GUARD_NUCLEUS_CONFIG_SOURCE_ARGV_KEY_RECOGNIZER_H

#include "nucleus/keyspace/key_path.h"

#include <functional>

namespace nucleus {

// Tests whether a mapped key path is a declared target. The argv source asks this
// AFTER syntactic mapping -- the schema-authoritative validation is a separate,
// later step from segmentation. Supplied by the host (it bridges to the schema
// registry's surface) so the seam header stays free of any registry dependency,
// preserving the flat topology.
using key_recognizer = std::function<bool(const key_path &)>;

}

#endif
