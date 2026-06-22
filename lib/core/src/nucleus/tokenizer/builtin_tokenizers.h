#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_BUILTIN_TOKENIZERS_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_BUILTIN_TOKENIZERS_H

#include "nucleus/tokenizer/tokenizer.h"

namespace nucleus {

// The env tokenizer: ${env.<name>} expands to the value of environment variable
// <name> at resolve time, or fails with missing_field when it is unset. A
// wildcard category -- any variable name is valid input rather than a fixed
// enumeration -- so it reads std::getenv per lookup with no caching.
tokenizer make_env_tokenizer();

// The string tokenizer: pure string operations over named, typed arguments
// (value is the string subject; values is a list; pos/count are ints).
//   ${string.upper(value=s)}                      -> s upcased (ASCII)
//   ${string.lower(value=s)}                      -> s downcased (ASCII)
//   ${string.trim(value=s)}                       -> s with surrounding ASCII whitespace removed
//   ${string.length(value=s)}                     -> the byte length of s as a decimal string
//   ${string.replace(value=s, from=a, to=b)}      -> s with every a replaced by b
//   ${string.substr(value=s, pos=p[, count=n])}   -> s from byte offset p, optionally n bytes
//   ${string.concat(values=[a,b,...][, separator=sep])} -> the elements joined by sep (default "")
tokenizer make_string_tokenizer();

}

#endif
