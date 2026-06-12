#include "nucleus/format.h"

#include "nucleus/config_source/path_text.h"

#include "nucleus/tokenizer/scope_keys.h"

#include <ranges>

namespace nucleus {

namespace {

const scope_frame *innermost_file_frame(std::span<const scope_frame> frames)
{
    for(const auto &f : frames | std::views::reverse)
        if(f.which == scope_frame::kind::file)
            return &f;
    return nullptr;
}

}

token_result resolve_scope_key(std::string_view key, std::span<const scope_frame> frames)
{
    const bool is_file_key = key == "file_name" || key == "file_directory"
                          || key == "file_path" || key == "file_stem";
    if(!is_file_key)
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("unknown scope key 'scope.{}'", key)));

    const scope_frame *file = innermost_file_frame(frames);
    if(!file)
        return unexpected(resolve_error(resolve_errc::out_of_scope_context,
                                  nucleus::format("'scope.{}' resolved outside any file frame", key)));

    if(key == "file_name") return path_to_text(file->file_path.filename());
    if(key == "file_directory") return path_to_text(file->file_path.parent_path());
    if(key == "file_path") return path_to_text(file->file_path);
    return path_to_text(file->file_path.stem());
}

bool is_location_category(std::string_view category) noexcept
{
    return category == "file" || category == "dir" || category == "self";
}

token_result resolve_location_key(std::string_view category, std::string_view key,
                                  std::span<const scope_frame> frames)
{
    const scope_frame *file = innermost_file_frame(frames);
    if(!file)
        return unexpected(resolve_error(resolve_errc::out_of_scope_context,
                                  nucleus::format("'{}.{}' resolved outside any file frame",
                                                  category, key)));
    const auto &path = file->file_path;

    if(category == "self")
    {
        if(key == "path") return path_to_text(path);
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("unknown key 'self.{}'", key)));
    }
    if(category == "dir")
    {
        if(key == "path") return path_to_text(path.parent_path());
        if(key == "name") return path_to_text(path.parent_path().filename());
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("unknown key 'dir.{}'", key)));
    }
    // category == "file"
    if(key == "name") return path_to_text(path.filename());
    if(key == "path") return path_to_text(path);
    if(key == "stem") return path_to_text(path.stem());
    return unexpected(resolve_error(resolve_errc::missing_field,
                              nucleus::format("unknown key 'file.{}'", key)));
}

}
