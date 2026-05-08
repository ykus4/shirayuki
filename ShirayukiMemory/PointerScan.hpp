#ifndef SHIRAYUKI_POINTER_SCAN_HPP
#define SHIRAYUKI_POINTER_SCAN_HPP

#include "ShirayukiMemory.hpp"
#include <deque>
#include <functional>

namespace Shirayuki {

// A complete pointer chain from a static base to the target
struct PointerChain {
    std::string moduleName;  // module containing the base address
    uintptr_t moduleOffset;  // offset within the module (ASLR-independent)
    std::deque<int64_t> offsets; // chain of dereference offsets

    // Resolve the chain at runtime (returns 0 on failure)
    uintptr_t resolve() const;

    // Serialize to string representation
    std::string toString() const;
};

// Pointer scan configuration
struct PointerScanConfig {
    uintptr_t targetAddress = 0;
    uint32_t maxDepth = 4;
    int64_t maxOffset = 0x1000;
    size_t maxResults = 100;
    bool scanHeap = true;
    bool scanStack = false;

    // Progress callback: (currentDepth, maxDepth)
    std::function<void(uint32_t, uint32_t)> progressCallback;
};

namespace PointerScanner {
    std::vector<PointerChain> scan(const PointerScanConfig &config);
    std::vector<uintptr_t> findPointersTo(uintptr_t targetAddress,
                                          int64_t maxOffset = 0x1000);
    bool validate(const PointerChain &chain, uintptr_t expectedTarget);
}

} // namespace Shirayuki

#endif // SHIRAYUKI_POINTER_SCAN_HPP
