#pragma once

#include "ShirayukiMemory.hpp"

#include <string>
#include <vector>

/// Scan and narrow operations for the search tab.
///
/// This used to be an `extern "C"` API handing back malloc'd arrays, on the
/// belief that C++ could not be used from `.mm` inside a dispatch block. That
/// belief was false (see CLAUDE.md), and the C boundary was actively harmful:
/// the value width crossed it as a bare `size_t` that the caller then read into
/// a fixed `unsigned char[8]`, so scanning for a string longer than 8 characters
/// overflowed the caller's stack buffer.
///
/// Snapshots are now captured here, where the value width is known, so no caller
/// has to size a buffer correctly.
namespace SYScan {

enum class Error {
    None,
    UnknownType,  // type tag not recognised
    InvalidValue, // input not parseable for the requested type
    EmptyPattern, // hex/regex/string search with no needle
    BadRegex,     // regex failed to compile
};

/// What to search for. `typeTag` is either a ValueType tag ("int32", "i32",
/// "float", ...) or one of the non-numeric modes "hex", "regex", "string".
struct Request {
    std::string typeTag = "int32";
    std::string input;
    size_t maxResults = Shirayuki::kMaxScanResults;
    size_t maxRegionSize = Shirayuki::kMaxRegionSize;
};

struct Result {
    std::vector<uintptr_t> addresses;

    /// Value bytes captured at each hit, `addresses.size() * valueSize` long.
    /// Empty for the non-numeric modes, where there is no fixed-width value to
    /// snapshot and hence nothing to narrow against.
    std::vector<uint8_t> snapshots;

    /// Width of one value in `snapshots`, 0 for non-numeric modes. Always <=
    /// Shirayuki::kMaxValueSize when non-zero.
    size_t valueSize = 0;

    Error error = Error::None;

    bool ok() const {
        return error == Error::None;
    }
    /// True when this result can be narrowed (i.e. carries typed snapshots).
    bool narrowable() const {
        return valueSize > 0;
    }
    size_t count() const {
        return addresses.size();
    }
    const uint8_t *snapshotAt(size_t index) const {
        if (valueSize == 0 || (index + 1) * valueSize > snapshots.size())
            return nullptr;
        return snapshots.data() + index * valueSize;
    }
};

/// Human-readable reason for a failed request.
std::string describe(Error error);

/// Scan every readable/writable region.
Result scanAll(const Request &request);

/// Scan one region. Exposed for tests and for rescanning a known range.
Result scanRegion(uintptr_t start, size_t len, const Request &request);

struct NarrowRequest {
    /// Previous result. `snapshots` and `valueSize` must come from a prior scan
    /// or narrow of the same type.
    std::vector<uintptr_t> addresses;
    std::vector<uint8_t> snapshots;
    size_t valueSize = 0;

    std::string typeTag = "int32";
    Shirayuki::CompareMode mode = Shirayuki::CompareMode::Changed;

    /// Needed only by Exact, GreaterThan and LessThan.
    std::string compareInput;
};

/// Filter candidates by comparing their current values against the snapshots.
///
/// Comparison is numeric, via Shirayuki::compareValues. Doing it with memcmp —
/// as the ObjC implementation this replaces did — inverts "increased" and
/// "decreased" for every multi-byte type on little-endian ARM64.
Result narrow(const NarrowRequest &request);

/// Write `input` (parsed for `typeTag`) to every address. Returns the number of
/// successful writes, and reports a parse failure rather than writing zeroes.
size_t writeAll(const std::vector<uintptr_t> &addresses, const std::string &typeTag,
                const std::string &input, Error &outError);

} // namespace SYScan
