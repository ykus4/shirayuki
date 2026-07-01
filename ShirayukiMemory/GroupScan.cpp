#include "GroupScan.hpp"
#include "ShirayukiConfig.hpp"
#include <cstring>

namespace Shirayuki {

bool GroupScanner::matchAt(uintptr_t base, const GroupPattern &pattern) {
    if (pattern.fields.empty())
        return false;

    for (auto &f : pattern.fields) {
        std::vector<uint8_t> buf(f.value.size());
        uintptr_t addr = static_cast<uintptr_t>(static_cast<int64_t>(base) + f.offset);
        if (Memory::read(addr, buf.data(), buf.size()) != Status::Success)
            return false;
        if (memcmp(buf.data(), f.value.data(), buf.size()) != 0)
            return false;
    }
    return true;
}

std::vector<uintptr_t> GroupScanner::scan(const GroupPattern &pattern, size_t maxResults) {
    std::vector<uintptr_t> results;
    if (pattern.fields.empty() || !maxResults)
        return results;

    // Anchor field: the one with offset==0 if it exists, else the first field.
    const GroupField *anchor = &pattern.fields.front();
    for (auto &f : pattern.fields) {
        if (f.offset == 0) {
            anchor = &f;
            break;
        }
    }

    auto regions = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
    for (auto &r : regions) {
        if (results.size() >= maxResults)
            break;
        if (r.size > kMaxRegionSize)
            continue;

        // Byte-by-byte anchor sweep. memcmp keeps the anchor check type-agnostic
        // so future anchor types (mixed integer/float/string) work without changes.
        const uint8_t *buf = reinterpret_cast<const uint8_t *>(r.start);
        size_t anchorLen = anchor->value.size();
        if (anchorLen == 0 || r.size < anchorLen)
            continue;

        for (size_t i = 0; i + anchorLen <= r.size; i++) {
            if (memcmp(buf + i, anchor->value.data(), anchorLen) != 0)
                continue;

            // Adjust `base` so that anchor.offset lands at (r.start + i).
            uintptr_t base =
                static_cast<uintptr_t>(static_cast<int64_t>(r.start + i) - anchor->offset);
            if (matchAt(base, pattern)) {
                results.push_back(base);
                if (results.size() >= maxResults)
                    break;
            }
        }
    }

    return results;
}

} // namespace Shirayuki
