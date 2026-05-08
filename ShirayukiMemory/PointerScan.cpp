#include "PointerScan.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>

namespace Shirayuki {

uintptr_t PointerChain::resolve() const {
    ImageInfo img = Image::find(moduleName);
    if (!img.isValid())
        return 0;

    uintptr_t current = img.base + moduleOffset;

    for (auto off : offsets) {
        uintptr_t ptr = 0;
        if (Memory::read(current, &ptr, sizeof(ptr)) != Status::Success)
            return 0;
        if (ptr == 0)
            return 0;
        current = ptr + off;
    }

    return current;
}

std::string PointerChain::toString() const {
    std::ostringstream ss;
    // Extract short module name
    size_t lastSlash = moduleName.rfind('/');
    std::string shortName =
        (lastSlash != std::string::npos) ? moduleName.substr(lastSlash + 1) : moduleName;

    ss << shortName << "+0x" << std::hex << moduleOffset;
    for (auto off : offsets) {
        ss << " -> [";
        if (off >= 0)
            ss << "+0x" << std::hex << off;
        else
            ss << "-0x" << std::hex << (-off);
        ss << "]";
    }
    return ss.str();
}

std::vector<uintptr_t> PointerScanner::findPointersTo(uintptr_t targetAddress, int64_t maxOffset) {
    std::vector<uintptr_t> results;
    auto regions = Memory::listRegions(VM_PROT_READ);

    uintptr_t rangeStart = (maxOffset > (int64_t)targetAddress) ? 0 : targetAddress - maxOffset;
    uintptr_t rangeEnd = targetAddress + maxOffset;

    for (auto &region : regions) {
        if (region.size < sizeof(uintptr_t))
            continue;

        const size_t chunkSize = 1024 * 1024; // 1MB chunks
        for (size_t off = 0; off < region.size; off += chunkSize) {
            size_t readLen = std::min(chunkSize, region.size - off);
            std::vector<uint8_t> buf(readLen);

            if (Memory::read(region.start + off, buf.data(), readLen) != Status::Success) {
                continue;
            }

            for (size_t i = 0; i + sizeof(uintptr_t) <= readLen; i += sizeof(uintptr_t)) {
                uintptr_t val;
                memcpy(&val, buf.data() + i, sizeof(uintptr_t));

                if (val >= rangeStart && val <= rangeEnd) {
                    results.push_back(region.start + off + i);
                }
            }
        }

        if (results.size() >= 10000)
            break;
    }

    return results;
}

// Recursive scan with image list cached outside
struct ScanContext {
    const PointerScanConfig &config;
    std::vector<PointerChain> &results;
    std::set<uintptr_t> visited;
    const std::vector<ImageInfo> &images; // cached
};

static void scanRecursive(ScanContext &ctx, uintptr_t target, std::deque<int64_t> &currentOffsets,
                          uint32_t depth) {
    if (depth >= ctx.config.maxDepth)
        return;
    if (ctx.results.size() >= ctx.config.maxResults)
        return;

    if (ctx.config.progressCallback) {
        ctx.config.progressCallback(depth, ctx.config.maxDepth);
    }

    auto pointers = PointerScanner::findPointersTo(target, ctx.config.maxOffset);

    for (auto ptrAddr : pointers) {
        if (ctx.results.size() >= ctx.config.maxResults)
            return;
        if (ctx.visited.count(ptrAddr))
            continue;
        ctx.visited.insert(ptrAddr);

        uintptr_t ptrValue = 0;
        Memory::read(ptrAddr, &ptrValue, sizeof(ptrValue));
        int64_t offset = (int64_t)target - (int64_t)ptrValue;

        currentOffsets.push_front(offset);

        // Check if this pointer is in a known module
        for (auto &img : ctx.images) {
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
        if (depth + 1 < ctx.config.maxDepth) {
            scanRecursive(ctx, ptrAddr, currentOffsets, depth + 1);
        }

        currentOffsets.pop_front();
        ctx.visited.erase(ptrAddr);
    }
}

std::vector<PointerChain> PointerScanner::scan(const PointerScanConfig &config) {
    std::vector<PointerChain> results;
    if (!config.targetAddress)
        return results;

    // Cache image list once
    auto images = Image::listAll();

    ScanContext ctx{config, results, {}, images};
    std::deque<int64_t> offsets;

    // Check if target itself is directly in a module
    for (auto &img : images) {
        if (config.targetAddress >= img.base && config.targetAddress < img.base + 0x10000000) {
            PointerChain direct;
            direct.moduleName = img.name;
            direct.moduleOffset = config.targetAddress - img.base;
            results.push_back(direct);
        }
    }

    scanRecursive(ctx, config.targetAddress, offsets, 0);

    return results;
}

bool PointerScanner::validate(const PointerChain &chain, uintptr_t expectedTarget) {
    return chain.resolve() == expectedTarget;
}

} // namespace Shirayuki
