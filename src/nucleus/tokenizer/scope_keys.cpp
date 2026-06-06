#include "nucleus/tokenizer/scope_keys.h"

#include "nucleus/format.h"

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
        return fail(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("unknown scope key 'scope.{}'", key)));

    const scope_frame *file = innermost_file_frame(frames);
    if(!file)
        return fail(resolve_error(resolve_errc::out_of_scope_context,
                                  nucleus::format("'scope.{}' resolved outside any file frame", key)));

    if(key == "file_name") return file->file_path.filename().string();
    if(key == "file_directory") return file->file_path.parent_path().string();
    if(key == "file_path") return file->file_path.string();
    return file->file_path.stem().string();
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
        return fail(resolve_error(resolve_errc::out_of_scope_context,
                                  nucleus::format("'{}.{}' resolved outside any file frame",
                                                  category, key)));
    const auto &path = file->file_path;

    if(category == "self")
    {
        if(key == "path") return path.string();
        return fail(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("unknown key 'self.{}'", key)));
    }
    if(category == "dir")
    {
        if(key == "path") return path.parent_path().string();
        if(key == "name") return path.parent_path().filename().string();
        return fail(resolve_error(resolve_errc::missing_field,
                                  nucleus::format("unknown key 'dir.{}'", key)));
    }
    // category == "file"
    if(key == "name") return path.filename().string();
    if(key == "path") return path.string();
    if(key == "stem") return path.stem().string();
    return fail(resolve_error(resolve_errc::missing_field,
                              nucleus::format("unknown key 'file.{}'", key)));
}

}
