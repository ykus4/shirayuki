#ifndef SHIRAYUKI_CONFIG_HPP
#define SHIRAYUKI_CONFIG_HPP

#include <cstddef>
#include <cstdint>

namespace Shirayuki {

// --- Value types ---

// Widest ValueType in bytes. Every staging buffer for a single scanned value is
// sized to this; ValueType.cpp static_asserts the descriptor table against it.
inline constexpr size_t kMaxValueSize = 8;

// Byte width assumed when a type tag cannot be resolved.
inline constexpr size_t kDefaultValueSize = 4;

// --- Scanning ---

// Maximum number of scan results returned by SYScanAll
inline constexpr size_t kMaxScanResults = 2000;

// Maximum memory region size to scan (100 MB)
inline constexpr size_t kMaxRegionSize = 100 * 1024 * 1024;

// Chunk size used when copying target memory before scanning it (1 MB).
// Scanning a copy rather than dereferencing the region directly keeps a
// concurrent free()/munmap() in the host app from faulting the whole process.
inline constexpr size_t kScanChunkSize = 1024 * 1024;

// Maximum raw pointer-to results per findPointersTo call
inline constexpr size_t kPointerScanMaxRawResults = 10000;

// Heuristic upper bound of a module's VM size (256 MB). Used both by the
// pointer scanner and by image-relative pattern scans.
inline constexpr uintptr_t kModuleMaxSize = 0x10000000;

// Lowest address treated as belonging to a thread stack.
inline constexpr uintptr_t kStackRegionMinAddress = 0x100000000ULL;

// Upper bound on a single Hex::dump request, so a bad length cannot turn into
// an unbounded allocation.
inline constexpr size_t kMaxHexDumpLength = 64 * 1024;

// Bytes per line in hex dumps.
inline constexpr size_t kHexDumpBytesPerLine = 16;

// Maximum compiled-regex cache entries before the cache is cleared.
inline constexpr size_t kMaxRegexCacheEntries = 32;

// --- Pointer scan defaults ---

inline constexpr int kPointerScanDefaultDepth = 3;
inline constexpr int kPointerScanMaxDepth = 8;
inline constexpr uintptr_t kPointerScanDefaultMaxOffset = 0x1000;
inline constexpr size_t kPointerScanDefaultMaxResults = 100;

// --- Background worker intervals (milliseconds) ---

inline constexpr unsigned kFreezeIntervalMs = 16;
inline constexpr unsigned kWatchIntervalMs = 100;

// Clamp for caller-supplied worker intervals.
inline constexpr unsigned kMinWorkerIntervalMs = 1;
inline constexpr unsigned kMaxWorkerIntervalMs = 5000;

} // namespace Shirayuki

#endif // SHIRAYUKI_CONFIG_HPP
