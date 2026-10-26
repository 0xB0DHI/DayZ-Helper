#include "placeholder_reader.h"
#include <limits>

bool UserRange(std::uint64_t address, std::size_t length) {
    return length > 0 && length <= 4096 && address >= 0x10000 &&
        address < 0x0000800000000000ULL &&
        static_cast<std::uint64_t>(length - 1) <= 0x00007FFFFFFFFFFFULL - address;
}

bool AddAddress(std::uint64_t base, std::uint64_t displacement,
                std::uint64_t& result) {
    if (base == 0 || displacement > std::numeric_limits<std::uint64_t>::max() - base)
        return false;
    result = base + displacement;
    return UserRange(result, 1);
}
