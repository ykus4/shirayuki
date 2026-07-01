#pragma once
#include <cstddef>
#include <cstdint>

namespace Shirayuki {

// Maximum number of scan results returned by SYScanAll
static constexpr size_t kMaxScanResults = 2000;

// Maximum memory region size to scan (100 MB)
static constexpr size_t kMaxRegionSize = 100 * 1024 * 1024;

// Chunk size for pointer scan reads (1 MB)
static constexpr size_t kPointerScanChunkSize = 1 * 1024 * 1024;

// Maximum raw pointer-to results per findPointersTo call
static constexpr size_t kPointerScanMaxRawResults = 10000;

// Heuristic upper bound of a Mach-O image's contiguous VM range.
// Larger than any single iOS binary observed in practice (main app ~50–200 MB
// including __TEXT+__DATA+__LINKEDIT). Kept as 256 MB to give headroom for
// unusually large binaries without letting the pointer scan wander into
// unrelated adjacent regions.
static constexpr size_t kMBytes = 1024 * 1024;
static constexpr uintptr_t kPointerScanModuleMaxSize = 256 * kMBytes;

} // namespace Shirayuki
