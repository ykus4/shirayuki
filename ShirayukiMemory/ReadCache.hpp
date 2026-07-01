#ifndef SHIRAYUKI_READ_CACHE_HPP
#define SHIRAYUKI_READ_CACHE_HPP

#include "ShirayukiMemory.hpp"

namespace Shirayuki {

// A small TTL-based read cache. Freeze/Watch loops call `read` many times per
// tick for the same addresses; caching keeps the vm_read_overwrite pressure down
// when the TTL is short (a few ms). Set TTL to 0 to disable caching entirely.
//
// Not thread-safe by design: each poll thread owns its own cache instance to
// avoid contention. Global helpers can be added later if a shared cache is needed.
class ReadCache {
  public:
    ReadCache() = default;

    // Set TTL in milliseconds. TTL of 0 disables caching (every read hits Memory::read).
    void setTTL(uint32_t ms) {
        m_ttlMs = ms;
    }
    uint32_t ttl() const {
        return m_ttlMs;
    }

    // Read `len` bytes at `address` into `out`. Returns Success on hit or on
    // successful uncached read; propagates the underlying status otherwise.
    Status read(uintptr_t address, void *out, size_t len);

    // Explicit invalidation — call after writing to `address` if the caller
    // needs subsequent reads to see the new bytes immediately.
    void invalidate(uintptr_t address);
    void clear();

  private:
    struct Entry {
        std::vector<uint8_t> bytes;
        uint64_t timestampMs = 0;
    };
    std::vector<std::pair<uintptr_t, Entry>> m_entries;
    uint32_t m_ttlMs = 0;

    static uint64_t nowMs();
};

} // namespace Shirayuki

#endif // SHIRAYUKI_READ_CACHE_HPP
