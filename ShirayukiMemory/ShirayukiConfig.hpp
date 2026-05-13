#pragma once
#include <cstddef>
#include <cstdint>

namespace Shirayuki {

// Maximum number of scan results returned by SYScanAll
static constexpr size_t kMaxScanResults = 2000;

// Maximum memory region size to scan (100 MB)
static constexpr size_t kMaxRegionSize = 100 * 1024 * 1024;

// Chunk size for pointer scan reads (1 MB)
static constexpr size_t kPointerScanChunkSize = 1024 * 1024;

// Maximum raw pointer-to results per findPointersTo call
static constexpr size_t kPointerScanMaxRawResults = 10000;

// Heuristic upper bound of a module's VM size (256 MB)
static constexpr uintptr_t kPointerScanModuleMaxSize = 0x10000000;

} // namespace Shirayuki
