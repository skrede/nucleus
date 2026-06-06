#ifndef HPP_GUARD_NUCLEUS_SOURCE_PATH_TEXT_H
#define HPP_GUARD_NUCLEUS_SOURCE_PATH_TEXT_H

#include <string>
#include <filesystem>

namespace nucleus {

// The single, canonical filesystem-path -> text conversion for the whole engine.
//
// std::filesystem::path::string() is platform-divergent: on Windows it narrows
// the native wchar_t representation through the active code page (lossy for
// non-ASCII) and yields backslash separators, whereas POSIX yields UTF-8 with
// forward slashes. The same config document therefore produces platform-
// dependent token text. To make path-derived tokens (scope/file/dir/self) and
// discovery results stable across platforms, every path->text conversion routes
// through here: generic_u8string() gives forward-slash separators and a stable
// UTF-8 encoding on every platform, which we hold as a plain std::string.
[[nodiscard]] inline std::string path_to_text(const std::filesystem::path &p)
{
    const std::u8string u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

}

#endif
