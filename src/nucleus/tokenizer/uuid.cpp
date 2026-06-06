#include "nucleus/tokenizer/uuid.h"

#include <array>
#include <random>
#include <cstdint>

namespace nucleus {

namespace {

std::uint64_t random_bits()
{
    // thread_local engine seeded once per thread; std::random_device feeds the
    // seed so distinct runs/threads diverge. 64-bit draws are assembled from the
    // engine's native width to fill the 128-bit UUID in two pulls.
    static thread_local std::mt19937_64 engine(std::random_device{}());
    return engine();
}

char hex_digit(unsigned value)
{
    return static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
}

}

std::string generate_uuid_v4()
{
    std::array<std::uint8_t, 16> bytes{};
    std::uint64_t hi = random_bits();
    std::uint64_t lo = random_bits();
    for(int i = 0; i < 8; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(hi >> (8 * i));
        bytes[8 + i] = static_cast<std::uint8_t>(lo >> (8 * i));
    }

    // Stamp the version (4) and variant (10xx) bits per RFC 4122.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

    std::string out;
    out.reserve(36);
    for(std::size_t i = 0; i < bytes.size(); ++i)
    {
        if(i == 4 || i == 6 || i == 8 || i == 10)
            out += '-';
        out += hex_digit(bytes[i] >> 4);
        out += hex_digit(bytes[i] & 0x0F);
    }
    return out;
}

}
