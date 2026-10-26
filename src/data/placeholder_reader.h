#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Public integration point. No game-memory transport is shipped here.
// The fake-memory suite supplies its own reader to the templated samplers.
class PlaceholderReader {
public:
    void BeginPass() { reads_ = logicalReads_ = failures_ = 0; }
    void SetPassCap(std::uint32_t cap) { cap_ = std::max<std::uint32_t>(1, std::min(cap, hardCap_)); }
    void SetHardCap(std::uint32_t cap) {
        hardCap_ = std::max<std::uint32_t>(1, cap);
        cap_ = hardCap_;
    }
    std::uint32_t PassCap() const { return cap_; }
    bool ReadRaw(std::uint64_t, void*, std::size_t) {
        ++failures_;
        return false;
    }
    bool ReadRawGrouped(std::uint64_t address, void* result, std::size_t length,
                        std::uint32_t) {
        return ReadRaw(address, result, length);
    }
    template<class T> bool Read(std::uint64_t address, T& result) {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 4096);
        return ReadRaw(address, &result, sizeof(result));
    }
    std::uint32_t Reads() const { return reads_; }
    std::uint32_t LogicalReads() const { return logicalReads_; }
    std::uint32_t Failures() const { return failures_; }
private:
    std::uint32_t hardCap_ = 1, cap_ = 1;
    std::uint32_t reads_ = 0, logicalReads_ = 0, failures_ = 0;
};

bool UserRange(std::uint64_t address, std::size_t length);
bool AddAddress(std::uint64_t base, std::uint64_t displacement,
                std::uint64_t& result);
