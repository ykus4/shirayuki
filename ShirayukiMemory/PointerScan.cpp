#include "PointerScan.hpp"
#include <algorithm>
#include <cstring>
#include <set>

namespace Shirayuki {

// Resolve a pointer chain at runtime
uintptr_t PointerChain::resolve() const {
    ImageInfo img = Image::find(moduleName);
    if (!img.isValid()) return 0;

    uintptr_t current = img.base + moduleOffset;

    for (size_t i = 0; i < offsets.size(); i++) {
        // Read pointer at current address
        uintptr_t ptr = 0;
        if (Memory::read(current, &ptr, sizeof(ptr)) != Status::Success) return 0;
        if (ptr == 0) return 0;

        // Apply offset
        current = ptr + offsets[i];
    }

    return current;
}

// Find all pointers in readable memory that point within [target - maxOffset, target + maxOffset]
std::vector<uintptr_t> PointerScanner::findPointersTo(uintptr_t targetAddress,
                                                      int64_t maxOffset) {
    std::vector<uintptr_t> results;
    auto regions = Memory::listRegions(VM_PROT_READ);

    uintptr_t rangeStart = (maxOffset > (int64_t)targetAddress) ? 0 : targetAddress - maxOffset;
    uintptr_t rangeEnd = targetAddress + maxOffset;

    for (auto &region : regions) {
        if (!region.isReadable()) continue;
        if (region.size < sizeof(uintptr_t)) continue;

        // Read region in chunks to avoid huge allocations
        const size_t chunkSize = 1024 * 1024; // 1MB
        for (size_t off = 0; off < region.size; off += chunkSize) {
            size_t readLen = std::min(chunkSize, region.size - off);
            std::vector<uint8_t> buf(readLen);

            if (Memory::read(region.start + off, buf.data(), readLen) != Status::Success) {
                continue;
            }

            // Scan for pointers
            for (size_t i = 0; i + sizeof(uintptr_t) <= readLen; i += sizeof(uintptr_t)) {
                uintptr_t val;
                memcpy(&val, buf.data() + i, sizeof(uintptr_t));

                if (val >= rangeStart && val <= rangeEnd) {
                    results.push_back(region.start + off + i);
                }
            }
        }

        if (results.size() >= 10000) break; // safety limit for single-level
    }

    return results;
}

// Recursive pointer chain builder
struct ScanContext {
    const PointerScanConfig &config;
    std::vector<PointerChain> &results;
    std::set<uintptr_t> visited; // avoid cycles
};

static void scanRecursive(ScanContext &ctx, uintptr_t target,
                          std::vector<int64_t> &currentOffsets, int depth) {
    if (depth >= (int)ctx.config.maxDepth) return;
    if (ctx.results.size() >= ctx.config.maxResults) return;

    auto pointers = PointerScanner::findPointersTo(target, ctx.config.maxOffset);

    for (auto ptrAddr : pointers) {
        if (ctx.results.size() >= ctx.config.maxResults) return;
        if (ctx.visited.count(ptrAddr)) continue;
        ctx.visited.insert(ptrAddr);

        // Calculate offset: pointer value + offset = target
        uintptr_t ptrValue = 0;
        Memory::read(ptrAddr, &ptrValue, sizeof(ptrValue));
        int64_t offset = (int64_t)target - (int64_t)ptrValue;

        currentOffsets.insert(currentOffsets.begin(), offset);

        // Check if this pointer is in a known module (static base found)
        auto images = Image::listAll();
        for (auto &img : images) {
            if (ptrAddr >= img.base && ptrAddr < img.base + 0x10000000) {
                PointerChain chain;
                chain.moduleName = img.name;
                chain.moduleOffset = ptrAddr - img.base;
                chain.offsets = currentOffsets;
                ctx.results.push_back(chain);
                break;
            }
        }

        // Go deeper
        if (depth + 1 < (int)ctx.config.maxDepth) {
            scanRecursive(ctx, ptrAddr, currentOffsets, depth + 1);
        }

        currentOffsets.erase(currentOffsets.begin());
        ctx.visited.erase(ptrAddr);
    }
}

std::vector<PointerChain> PointerScanner::scan(const PointerScanConfig &config) {
    std::vector<PointerChain> results;
    if (!config.targetAddress) return results;

    ScanContext ctx{config, results, {}};
    std::vector<int64_t> offsets;

    // First check if target itself is directly in a module
    auto images = Image::listAll();
    for (auto &img : images) {
        if (config.targetAddress >= img.base &&
            config.targetAddress < img.base + 0x10000000) {
            // Direct static address — no chain needed
            PointerChain direct;
            direct.moduleName = img.name;
            direct.moduleOffset = config.targetAddress - img.base;
            results.push_back(direct);
        }
    }

    scanRecursive(ctx, config.targetAddress, offsets, 0);

    if (config.progressCallback) {
        config.progressCallback(1, 1); // signal completion
    }

    return results;
}

bool PointerScanner::validate(const PointerChain &chain, uintptr_t expectedTarget) {
    uintptr_t resolved = chain.resolve();
    return resolved == expectedTarget;
}

} // namespace Shirayuki
