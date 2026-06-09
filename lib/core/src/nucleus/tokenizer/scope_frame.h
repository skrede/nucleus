#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_SCOPE_FRAME_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_SCOPE_FRAME_H

#include <string>
#include <utility>
#include <filesystem>
#include <unordered_map>

namespace nucleus {

// A lexical frame on the resolver's innermost-first stack. Core ships two
// concrete frame kinds and the generic `bindings` map that lets a host model its
// own vocabulary without the core learning that vocabulary:
//
//   * file -- carries the path of the value's source location, surfaced through
//     the generic ${scope.file_*} keys. This is the one concrete scope frame
//     the core ships.
//   * param -- carries the bound arguments of a function invocation, keyed by
//     parameter name; reserved for hosts that model macros over the frame stack.
//   * generic -- a host-named frame category whose `bindings` answer
//     ${category.<name>} lookups directly. This is the mechanism by which a host
//     registers additional, vocabulary-specific scope categories WITHOUT baking
//     that vocabulary into the core.
//
// The `category` string is what a token must name (after ${) to reach this
// frame's bindings; for the file frame it is unused (file keys are dispatched by
// the reserved "scope" category).
struct scope_frame
{
    enum class kind
    {
        file,
        param,
        generic
    };

    kind which = kind::generic;
    std::string category;
    std::filesystem::path file_path;
    std::unordered_map<std::string, std::string> bindings;
};

}

#endif
