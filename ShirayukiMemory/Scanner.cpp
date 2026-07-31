#include "ShirayukiConfig.hpp"
#include "ShirayukiMemory.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace Shirayuki {

// =============================================================================
// Scanner
// =============================================================================

// Parse IDA-style pattern, build skip table for Boyer-Moore-like speedup
struct PatternData {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    size_t firstSolidByte = 0; // index of first non-wildcard for skip
};

static bool parseIdaPattern(const std::string &pattern, PatternData &out) {
    out.bytes.clear();
    out.mask.clear();
    out.firstSolidByte = 0;

    std::istringstream ss(pattern);
    std::string token;
    bool foundFirst = false;

    while (ss >> token) {
        if (token == "?" || token == "??") {
            out.bytes.push_back(0);
            out.mask.push_back(false);
        } else {
            unsigned int val;
            std::istringstream hexSS(token);
            if (!(hexSS >> std::hex >> val) || val > 0xFF)
                return false;
            out.bytes.push_back(static_cast<uint8_t>(val));
            out.mask.push_back(true);
            if (!foundFirst) {
                out.firstSolidByte = out.bytes.size() - 1;
                foundFirst = true;
            }
        }
    }

    return !out.bytes.empty();
}

namespace {

// Copy the target range in chunks and hand each chunk to `onChunk`.
//
// Nothing in the scanner dereferences target memory directly. The region list a
// scan works from is a vm_region snapshot, and the host app keeps running while
// the scan does: a range that was mapped when it was listed can be freed or
// unmapped moments later. A direct load then raises EXC_BAD_ACCESS and takes the
// whole host process down, whereas a failed Memory::read just skips the chunk.
//
// Consecutive chunks overlap by `overlap` bytes so a needle straddling a chunk
// boundary is still found. `onChunk` receives (buf, bufLen, reportLimit, base)
// and must only report matches at offsets below `reportLimit`, otherwise a match
// inside the overlap would be reported twice.
void forEachChunk(uintptr_t start, size_t len, size_t overlap,
                  const std::function<bool(const uint8_t *, size_t, size_t, uintptr_t)> &onChunk) {
    if (!len)
        return;

    std::vector<uint8_t> buf;
    size_t offset = 0;

    while (offset < len) {
        const size_t step = std::min(kScanChunkSize, len - offset);
        const size_t extended = std::min(step + overlap, len - offset);

        buf.resize(extended);
        if (Memory::read(start + offset, buf.data(), extended) == Status::Success) {
            if (!onChunk(buf.data(), extended, step, start + offset))
                return;
        } else {
            // Retry without the overlap: the tail may cross into an unmapped
            // page while the chunk proper is still readable.
            buf.resize(step);
            if (Memory::read(start + offset, buf.data(), step) == Status::Success) {
                if (!onChunk(buf.data(), step, step, start + offset))
                    return;
            }
        }

        offset += step;
    }
}

} // namespace

std::vector<uintptr_t> Scanner::findValueBytes(uintptr_t start, size_t len, const uint8_t *needle,
                                               size_t width, size_t stride) {
    std::vector<uintptr_t> results;
    if (!needle || !width || width > kMaxValueSize || len < width)
        return results;
    if (stride == 0)
        stride = width;

    forEachChunk(start, len, width - 1,
                 [&](const uint8_t *buf, size_t bufLen, size_t limit, uintptr_t base) {
                     // Keep the stride phase relative to `start`, so alignment
                     // means alignment within the scanned range rather than
                     // within whichever chunk happens to contain it.
                     const size_t phase = (base - start) % stride;
                     size_t i = (phase == 0) ? 0 : (stride - phase);
                     for (; i < limit && i + width <= bufLen; i += stride) {
                         if (memcmp(buf + i, needle, width) == 0)
                             results.push_back(base + i);
                     }
                     return true;
                 });

    return results;
}

// Shared inner scan loop — calls callback(matchOffset) for each hit, stops if callback returns
// false
static void scanPattern(const uint8_t *buf, size_t len, const PatternData &pat,
                        std::function<bool(size_t)> onMatch) {
    size_t patLen = pat.bytes.size();
    if (len < patLen)
        return;

    uint8_t anchor = pat.bytes[pat.firstSolidByte];

    for (size_t i = 0; i <= len - patLen;) {
        if (buf[i + pat.firstSolidByte] != anchor) {
            i++;
            continue;
        }
        bool found = true;
        for (size_t j = 0; j < patLen; j++) {
            if (pat.mask[j] && buf[i + j] != pat.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found && !onMatch(i))
            return;
        i++;
    }
}

std::vector<uintptr_t> Scanner::findPattern(uintptr_t start, size_t len,
                                            const std::string &pattern) {
    std::vector<uintptr_t> results;
    PatternData pat;
    if (!parseIdaPattern(pattern, pat))
        return results;

    forEachChunk(start, len, pat.bytes.size() - 1,
                 [&](const uint8_t *buf, size_t bufLen, size_t limit, uintptr_t base) {
                     scanPattern(buf, bufLen, pat, [&](size_t i) {
                         if (i < limit)
                             results.push_back(base + i);
                         return true;
                     });
                     return true;
                 });
    return results;
}

uintptr_t Scanner::findPatternFirst(uintptr_t start, size_t len, const std::string &pattern) {
    PatternData pat;
    if (!parseIdaPattern(pattern, pat))
        return 0;

    uintptr_t result = 0;
    forEachChunk(start, len, pat.bytes.size() - 1,
                 [&](const uint8_t *buf, size_t bufLen, size_t limit, uintptr_t base) {
                     scanPattern(buf, bufLen, pat, [&](size_t i) {
                         if (i >= limit)
                             return true;
                         result = base + i;
                         return false; // stop at first match
                     });
                     return result == 0; // stop chunking once found
                 });
    return result;
}

std::vector<uintptr_t> Scanner::findPatternInImage(const ImageInfo &img,
                                                   const std::string &pattern) {
    std::vector<uintptr_t> allResults;
    if (!img.isValid())
        return allResults;

    auto regions = Memory::listRegions(VM_PROT_READ);
    for (auto &region : regions) {
        if (region.start >= img.base && region.start < img.base + kModuleMaxSize) {
            auto results = findPattern(region.start, region.size, pattern);
            allResults.insert(allResults.end(), results.begin(), results.end());
        }
    }

    return allResults;
}

std::vector<uintptr_t> Scanner::findString(uintptr_t start, size_t len, const std::string &str) {
    std::vector<uintptr_t> results;
    if (str.empty() || len < str.size())
        return results;

    const uint8_t *needle = reinterpret_cast<const uint8_t *>(str.data());
    const size_t needleLen = str.size();

    forEachChunk(start, len, needleLen - 1,
                 [&](const uint8_t *buf, size_t bufLen, size_t limit, uintptr_t base) {
                     for (size_t i = 0; i < limit && i + needleLen <= bufLen; i++) {
                         if (memcmp(buf + i, needle, needleLen) == 0)
                             results.push_back(base + i);
                     }
                     return true;
                 });

    return results;
}

std::vector<uintptr_t> Scanner::findRegex(uintptr_t start, size_t len, const std::string &pattern) {
    std::vector<uintptr_t> results;
    if (pattern.empty() || !len)
        return results;

    // Cache compiled regexes to avoid recompilation on every scan.
    static std::mutex s_cacheMutex;
    static std::unordered_map<std::string, std::regex> s_regexCache;

    std::regex re;
    {
        std::lock_guard<std::mutex> cacheLock(s_cacheMutex);
        auto it = s_regexCache.find(pattern);
        if (it != s_regexCache.end()) {
            re = it->second;
        } else {
            try {
                re = std::regex(pattern, std::regex::ECMAScript | std::regex::optimize);
            } catch (...) {
                return results; // invalid pattern
            }
            // Every keystroke in the search field compiles a new pattern, so an
            // unbounded cache grows for the life of the process.
            if (s_regexCache.size() >= kMaxRegexCacheEntries)
                s_regexCache.clear();
            s_regexCache[pattern] = re;
        }
    }

    // Overlap by the longest string we are willing to reassemble across a chunk
    // boundary. A string longer than this may be tested twice as two fragments.
    const size_t kMaxStringLen = 4096;

    forEachChunk(start, len, kMaxStringLen,
                 [&](const uint8_t *buf, size_t bufLen, size_t limit, uintptr_t base) {
                     const char *chars = reinterpret_cast<const char *>(buf);
                     size_t i = 0;
                     while (i < bufLen) {
                         size_t j = i;
                         while (j < bufLen && chars[j] != '\0')
                             j++;

                         // Only report starts inside this chunk's own span, so a
                         // string seen again via the next chunk's overlap is not
                         // reported twice.
                         if (j > i && i < limit) {
                             std::string s(chars + i, j - i);
                             if (std::regex_search(s, re))
                                 results.push_back(base + i);
                         }

                         i = j + 1;
                     }
                     return true;
                 });

    return results;
}

// --- Narrowing ---

std::vector<Scanner::Candidate> Scanner::narrowResults(const std::vector<Candidate> &candidates,
                                                       ValueType type, CompareMode mode,
                                                       const void *compareValue) {

    std::vector<Candidate> filtered;
    const size_t sz = valueTypeSize(type);

    // Modes that diff against the snapshot need a snapshot wide enough to read;
    // modes that diff against a target value need that value.
    const bool needsSnapshot = (mode == CompareMode::Changed || mode == CompareMode::Unchanged ||
                                mode == CompareMode::Increased || mode == CompareMode::Decreased);
    const bool needsCompareValue =
        (mode == CompareMode::Exact || mode == CompareMode::GreaterThan ||
         mode == CompareMode::LessThan);

    if (needsCompareValue && !compareValue)
        return filtered;

    for (auto &c : candidates) {
        if (needsSnapshot && c.snapshotValue.size() < sz)
            continue;

        uint8_t currentBuf[kMaxValueSize] = {};
        if (Memory::read(c.address, currentBuf, sz) != Status::Success)
            continue;

        const uint8_t *snapshot = c.snapshotValue.data();
        const uint8_t *target = static_cast<const uint8_t *>(compareValue);
        bool keep = false;

        switch (mode) {
            case CompareMode::Exact:
                keep = (memcmp(currentBuf, target, sz) == 0);
                break;
            case CompareMode::Changed:
                keep = (memcmp(currentBuf, snapshot, sz) != 0);
                break;
            case CompareMode::Unchanged:
                keep = (memcmp(currentBuf, snapshot, sz) == 0);
                break;
            case CompareMode::Increased:
                keep = (compareValues(currentBuf, snapshot, type) > 0);
                break;
            case CompareMode::Decreased:
                keep = (compareValues(currentBuf, snapshot, type) < 0);
                break;
            case CompareMode::GreaterThan:
                keep = (compareValues(currentBuf, target, type) > 0);
                break;
            case CompareMode::LessThan:
                keep = (compareValues(currentBuf, target, type) < 0);
                break;
        }

        if (keep) {
            Candidate newCandidate;
            newCandidate.address = c.address;
            newCandidate.snapshotValue.assign(currentBuf, currentBuf + sz);
            filtered.push_back(newCandidate);
        }
    }

    return filtered;
}

} // namespace Shirayuki
