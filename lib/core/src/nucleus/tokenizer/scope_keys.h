#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_SCOPE_KEYS_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_SCOPE_KEYS_H

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/scope_frame.h"

#include <span>
#include <string_view>

namespace nucleus {

// Resolves a reserved ${scope.<key>} field against the innermost file frame.
// The four concrete core scope keys are file_name, file_directory, file_path,
// and file_stem -- the generic file-location vocabulary every host shares. An
// unknown key is missing_field; a known key with no file frame on the stack is
// out_of_scope_context. Host-specific frame categories are NOT handled here;
// they are reached through their own ${category.<name>} dispatch.
token_result resolve_scope_key(std::string_view key,
                                             std::span<const scope_frame> frames);

// Resolves the config-location path categories that derive from the innermost
// file frame:
//   ${file.name} / ${file.path} / ${file.stem}  -- the source file itself
//   ${dir.path} / ${dir.name}                    -- its containing directory
//   ${self.path}                                 -- alias for the file's full path
// These give a config document a stable handle on its own location for building
// sibling/relative paths. Returns out_of_scope_context with no file frame and
// missing_field for an unknown key under a known category. `category` is one of
// "file", "dir", "self"; the caller routes only those here.
token_result resolve_location_key(std::string_view category,
                                                std::string_view key,
                                                std::span<const scope_frame> frames);

bool is_location_category(std::string_view category) noexcept;

}

#endif
