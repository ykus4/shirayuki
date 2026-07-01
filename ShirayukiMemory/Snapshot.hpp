#ifndef SHIRAYUKI_SNAPSHOT_HPP
#define SHIRAYUKI_SNAPSHOT_HPP

#include "ShirayukiMemory.hpp"

namespace Shirayuki {

// A named snapshot of a contiguous memory range.
struct MemorySnapshot {
    std::string label;
    uintptr_t start = 0;
    std::vector<uint8_t> bytes;
};

// A single byte-level difference between two snapshots.
struct SnapshotDiff {
    uintptr_t address;
    uint8_t before;
    uint8_t after;
};

namespace SnapshotManager {
// Capture a snapshot of [start, start+len). Returns an empty snapshot on read failure.
MemorySnapshot capture(uintptr_t start, size_t len, const std::string &label);

// Save/load snapshots to disk (raw binary + tiny sidecar). Path is a base name;
// implementation appends ".bin" and ".meta" as needed.
bool save(const MemorySnapshot &snap, const std::string &basePath);
bool load(const std::string &basePath, MemorySnapshot &out);

// Compare two snapshots, returning per-byte differences. `maxDiffs` caps the result.
std::vector<SnapshotDiff> diff(const MemorySnapshot &before, const MemorySnapshot &after,
                               size_t maxDiffs = 4096);
} // namespace SnapshotManager

} // namespace Shirayuki

#endif // SHIRAYUKI_SNAPSHOT_HPP
