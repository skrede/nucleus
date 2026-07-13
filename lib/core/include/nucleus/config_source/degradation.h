#ifndef HPP_GUARD_NUCLEUS_CONFIG_SOURCE_DEGRADATION_H
#define HPP_GUARD_NUCLEUS_CONFIG_SOURCE_DEGRADATION_H

#include "nucleus/capability.h"

#include <string>

namespace nucleus {

// A feature that was asked for optionally but the source could not provide. It
// is surfaced (not dropped silently): emitted through the log_sink at warn level
// and recorded here so it can become value provenance ("field X degraded because
// source 'env' lacks nesting").
struct degradation
{
    capability cap;
    std::string note;
};

}

#endif
