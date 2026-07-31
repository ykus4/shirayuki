#ifndef SHIRAYUKI_GROUP_SCAN_HPP
#define SHIRAYUKI_GROUP_SCAN_HPP

#include "ShirayukiMemory.hpp"

namespace Shirayuki {

// A single field inside a struct being scanned for.
struct GroupField {
    int64_t offset; // signed offset from the struct base
    ValueType type;
    std::vector<uint8_t> value; // exact bytes to match
};

// A description of the whole struct: list of fields to match together.
struct GroupPattern {
    std::vector<GroupField> fields;
    size_t maxSpan() const {
        int64_t lo = 0, hi = 0;
        for (auto &f : fields) {
            if (f.offset < lo)
                lo = f.offset;
            int64_t end = f.offset + (int64_t)valueTypeSize(f.type);
            if (end > hi)
                hi = end;
        }
        return static_cast<size_t>(hi - lo);
    }
};

// Group / structure scan — finds base addresses `A` such that every field
// in `pattern` matches when read from `A + field.offset`. Skeleton: the
// implementation walks all readable regions and delegates to `matchAt` per slot.
namespace GroupScanner {
std::vector<uintptr_t> scan(const GroupPattern &pattern, size_t maxResults);

// Verify that a specific candidate address satisfies the pattern (all fields match).
bool matchAt(uintptr_t base, const GroupPattern &pattern);
} // namespace GroupScanner

} // namespace Shirayuki

#endif // SHIRAYUKI_GROUP_SCAN_HPP
