#ifndef SHIRAYUKI_POINTER_SCAN_HPP
#define SHIRAYUKI_POINTER_SCAN_HPP

#include "ShirayukiMemory.hpp"
#include <functional>

namespace Shirayuki {

// A single node in a pointer chain: base + offset -> next
struct PointerNode {
    uintptr_t address;   // address that holds the pointer
    int64_t offset;      // offset from the dereferenced value to reach next/target
};

// A complete pointer chain from a static base to the target
struct PointerChain {
    std::string moduleName;  // module containing the base address
    uintptr_t moduleOffset;  // offset within the module (ASLR-independent)
    std::vector<int64_t> offsets; // chain of dereference offsets

    // Resolve the chain at runtime (returns 0 on failure)
    uintptr_t resolve() const;
};

// Pointer scan configuration
struct PointerScanConfig {
    uintptr_t targetAddress = 0;
    uint32_t maxDepth = 4;           // max pointer chain depth
    int64_t maxOffset = 0x1000;      // max offset per level (+/-)
    size_t maxResults = 100;         // stop after N results
    bool scanHeap = true;            // include heap regions
    bool scanStack = false;          // include stack (slow)

    // Progress callback: (scannedRegions, totalRegions) -> shouldContinue
    std::function<void(size_t, size_t)> progressCallback;
};

namespace PointerScanner {
    // Find all pointer chains leading to targetAddress
    std::vector<PointerChain> scan(const PointerScanConfig &config);

    // Quick scan: find direct pointers to an address (depth=1)
    std::vector<uintptr_t> findPointersTo(uintptr_t targetAddress,
                                          int64_t maxOffset = 0x1000);

    // Validate an existing chain still resolves
    bool validate(const PointerChain &chain, uintptr_t expectedTarget);
}

} // namespace Shirayuki

#endif // SHIRAYUKI_POINTER_SCAN_HPP
