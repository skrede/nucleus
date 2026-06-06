#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_BUILTIN_TOKENIZERS_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_BUILTIN_TOKENIZERS_H

#include "nucleus/tokenizer/tokenizer.h"

namespace nucleus {

// The env tokenizer: ${env.<name>} expands to the value of environment variable
// <name> at resolve time, or fails with missing_field when it is unset. A
// wildcard category -- any variable name is valid input rather than a fixed
// enumeration -- so it reads std::getenv per lookup with no caching.
[[nodiscard]] tokenizer make_env_tokenizer();

// The uuid tokenizer: ${uuid.v4()} generates a fresh random version-4 UUID on
// each call. Non-deterministic by contract; persisting a generated value is the
// consumer's concern, not the tokenizer's.
[[nodiscard]] tokenizer make_uuid_tokenizer();

// The string tokenizer: pure string operations over already-resolved arguments.
//   ${string.upper(s)}        -> s upcased (ASCII)
//   ${string.lower(s)}        -> s downcased (ASCII)
//   ${string.trim(s)}         -> s with leading/trailing ASCII whitespace removed
//   ${string.replace(s,a,b)}  -> s with every occurrence of a replaced by b
//   ${string.substr(s,pos)}   -> s from byte offset pos to the end
//   ${string.substr(s,pos,n)} -> n bytes of s from byte offset pos
//   ${string.concat(a,b,...)} -> the arguments joined with no separator
//   ${string.length(s)}       -> the byte length of s as a decimal string
[[nodiscard]] tokenizer make_string_tokenizer();

}

#endif
