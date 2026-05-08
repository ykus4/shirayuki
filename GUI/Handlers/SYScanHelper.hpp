#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Scan all readable/writable regions for the given type+value.
// Returns heap-allocated array of matching addresses (caller must call SYScanFreeResults).
// *outCount = number of addresses found (capped at maxResults).
// *outValSize = byte width of the matched type (0 for pattern/regex/string).
uintptr_t *SYScanAll(const char *type, const char *input, size_t maxResults, size_t maxRegionSize,
                     size_t *outCount, size_t *outValSize);

// Scan a single memory region (used internally; exposed for testing).
uintptr_t *SYScanRegion(uintptr_t start, size_t len, const char *type, const char *input,
                        size_t *outCount, size_t *outValSize);

// Free results array returned by SYScan*.
void SYScanFreeResults(uintptr_t *results);

// Read `valSize` bytes from `addr` into `buf` (up to 8 bytes). Returns 1 on success.
int SYMemRead(uintptr_t addr, unsigned char *buf, size_t valSize);

#ifdef __cplusplus
}
#endif
