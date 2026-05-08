#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Scan a single memory region. Populates outAddrs/outCount (caller must free).
// outValSize is the byte size of each matched value (0 for pattern/regex/string).
// Returns heap-allocated array of uintptr_t; caller must call SYScanFreeResults().
uintptr_t *SYScanRegion(uintptr_t start, size_t len, const char *type, const char *input,
                        size_t *outCount, size_t *outValSize);

// Free results array returned by SYScanRegion.
void SYScanFreeResults(uintptr_t *results);

// Read `valSize` bytes from `addr` into `buf` (up to 8 bytes). Returns 1 on success.
int SYMemRead(uintptr_t addr, unsigned char *buf, size_t valSize);

#ifdef __cplusplus
}
#endif
