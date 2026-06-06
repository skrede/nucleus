#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_UUID_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_UUID_H

#include <string>

namespace nucleus {

// Generates a random RFC 4122 version-4 UUID as a canonical 8-4-4-4-12 lowercase
// hex string. Uses std::random_device-seeded generation; cross-platform with no
// external dependency. Each call yields a fresh value -- no caching, matching the
// "a generator is non-deterministic by contract" rule.
[[nodiscard]] std::string generate_uuid_v4();

}

#endif
