#pragma once
#include <cstddef>
#include <cstdint>

namespace Shirayuki {

static constexpr size_t kMBytes = 1024 * 1024;

// --- Value types ---

// Widest ValueType in bytes. Every staging buffer for a single scanned value is
// sized to this. Previously spelled as a bare 8 in roughly a dozen places.
static constexpr size_t kMaxValueSize = 8;

// Byte width assumed when a type tag cannot be resolved.
static constexpr size_t kDefaultValueSize = 4;

// --- Scanning ---

// Maximum number of scan results returned by SYScanAll
static constexpr size_t kMaxScanResults = 2000;

// Maximum memory region size to scan (100 MB)
static constexpr size_t kMaxRegionSize = 100 * kMBytes;

// Chunk size used when copying target memory before scanning or walking it.
// Scanning a copy rather than dereferencing the region directly keeps a
// concurrent free()/munmap() in the host app from faulting the whole process.
static constexpr size_t kScanChunkSize = 1 * kMBytes;

// Retained name for the pointer scanner, which uses the same chunk size.
static constexpr size_t kPointerScanChunkSize = kScanChunkSize;

// Maximum raw pointer-to results per findPointersTo call
static constexpr size_t kPointerScanMaxRawResults = 10000;

// Heuristic upper bound of a Mach-O image's contiguous VM range.
// Larger than any single iOS binary observed in practice (main app ~50–200 MB
// including __TEXT+__DATA+__LINKEDIT). Kept as 256 MB to give headroom for
// unusually large binaries without letting the pointer scan wander into
// unrelated adjacent regions.
static constexpr uintptr_t kPointerScanModuleMaxSize = 256 * kMBytes;

// Same heuristic, used by image-relative pattern scans.
static constexpr uintptr_t kModuleMaxSize = kPointerScanModuleMaxSize;

// Lowest address treated as belonging to a thread stack. Note this is a poor
// discriminator on arm64, where the entire user address space sits above 4 GB —
// RegionInfo::isStack() uses the kernel allocator tag instead.
static constexpr uintptr_t kStackRegionMinAddress = 0x100000000ULL;

// Upper bound on a single Hex::dump request, so a bad length cannot turn into an
// unbounded allocation.
static constexpr size_t kMaxHexDumpLength = 64 * 1024;

// Bytes per line in hex dumps.
static constexpr size_t kHexDumpBytesPerLine = 16;

// Maximum compiled-regex cache entries before the cache is cleared. Every
// keystroke in the search field compiles a new pattern.
static constexpr size_t kMaxRegexCacheEntries = 32;

// --- Background worker intervals (milliseconds) ---

static constexpr unsigned kFreezeIntervalMs = 16;
static constexpr unsigned kWatchIntervalMs = 100;

// Clamp for caller-supplied worker intervals: 0 would spin, and an unbounded
// value would stall shutdown.
static constexpr unsigned kMinWorkerIntervalMs = 1;
static constexpr unsigned kMaxWorkerIntervalMs = 5000;

} // namespace Shirayuki
