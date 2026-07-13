#ifndef HPP_GUARD_NUCLEUS_VERSION_H
#define HPP_GUARD_NUCLEUS_VERSION_H

#include <string_view>

// NOLINTBEGIN(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage): the version components must remain preprocessor macros so consumers can stringize them and test them in #if conditions.
#define NUCLEUS_VERSION_MAJOR 0
#define NUCLEUS_VERSION_MINOR 4
#define NUCLEUS_VERSION_PATCH 1
// NOLINTEND(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)

namespace nucleus {

std::string_view version() noexcept;

}

#endif
