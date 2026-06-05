#ifndef HPP_GUARD_NUCLEUS_VERSION_H
#define HPP_GUARD_NUCLEUS_VERSION_H

#include <string_view>

#define NUCLEUS_VERSION_MAJOR 0
#define NUCLEUS_VERSION_MINOR 0
#define NUCLEUS_VERSION_PATCH 0

namespace nucleus {

std::string_view version() noexcept;

}

#endif
