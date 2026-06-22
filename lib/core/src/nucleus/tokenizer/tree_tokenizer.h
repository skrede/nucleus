#ifndef HPP_GUARD_NUCLEUS_SRC_TOKENIZER_TREE_TOKENIZER_SHIM_H
#define HPP_GUARD_NUCLEUS_SRC_TOKENIZER_TREE_TOKENIZER_SHIM_H
// Delegates to the public header via a relative path to sidestep the include-path
// ordering issue when lib/core/src precedes lib/core/include for internal TUs.
#include "../../../include/nucleus/tokenizer/tree_tokenizer.h"
#endif
