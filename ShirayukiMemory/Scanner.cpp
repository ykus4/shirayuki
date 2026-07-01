#include "ShirayukiConfig.hpp"
#include "ShirayukiMemory.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace Shirayuki {

// Defined in ValueFormat.cpp — typed 3-way compare for the 10 ValueTypes.
int compareTypedBytes(const uint8_t *a, const uint8_t *b, ValueType type);

// --- Pattern parsing ---

struct PatternData {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    size_t firstSolidByte = 0;
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

// Shared inner loop for pattern/value/string. Callback returns false to stop early.
static void scanPatternInner(const uint8_t *buf, size_t len, const PatternData &pat,
                             const std::function<bool(size_t)> &onMatch) {
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

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    scanPatternInner(buf, len, pat, [&](size_t i) {
        results.push_back(start + i);
        return true;
    });
    return results;
}

uintptr_t Scanner::findPatternFirst(uintptr_t start, size_t len, const std::string &pattern) {
    PatternData pat;
    if (!parseIdaPattern(pattern, pat))
        return 0;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    uintptr_t result = 0;
    scanPatternInner(buf, len, pat, [&](size_t i) {
        result = start + i;
        return false;
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
        if (region.start >= img.base && region.start < img.base + kPointerScanModuleMaxSize) {
            auto results = findPattern(region.start, region.size, pattern);
            allResults.insert(allResults.end(), results.begin(), results.end());
        }
    }

    return allResults;
}

// --- Contiguous-byte scan: shared between findString / anything that matches a fixed buffer ---
static void scanBytes(const uint8_t *buf, size_t len, const uint8_t *needle, size_t needleLen,
                      const std::function<bool(size_t)> &onMatch) {
    if (!needleLen || len < needleLen)
        return;
    for (size_t i = 0; i <= len - needleLen; i++) {
        if (memcmp(buf + i, needle, needleLen) == 0) {
            if (!onMatch(i))
                return;
        }
    }
}

std::vector<uintptr_t> Scanner::findString(uintptr_t start, size_t len, const std::string &str) {
    std::vector<uintptr_t> results;
    if (str.empty())
        return results;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    scanBytes(buf, len, reinterpret_cast<const uint8_t *>(str.data()), str.size(), [&](size_t i) {
        results.push_back(start + i);
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
                s_regexCache[pattern] = re;
            } catch (...) {
                return results;
            }
        }
    }

    const char *buf = reinterpret_cast<const char *>(start);
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && buf[j] != '\0')
            j++;

        if (j > i) {
            std::string s(buf + i, j - i);
            if (std::regex_search(s, re)) {
                results.push_back(start + i);
            }
        }

        i = j + 1;
    }

    return results;
}

// --- Narrowing ---

std::vector<Scanner::Candidate> Scanner::narrowResults(const std::vector<Candidate> &candidates,
                                                       ValueType type, CompareMode mode,
                                                       const void *compareValue) {
    std::vector<Candidate> filtered;
    size_t sz = valueTypeSize(type);

    for (auto &c : candidates) {
        uint8_t currentBuf[8] = {};
        if (Memory::read(c.address, currentBuf, sz) != Status::Success)
            continue;

        bool keep = false;

        switch (mode) {
            case CompareMode::Exact:
                if (compareValue)
                    keep = (memcmp(currentBuf, compareValue, sz) == 0);
                break;
            case CompareMode::Changed:
                keep = (memcmp(currentBuf, c.snapshotValue.data(), sz) != 0);
                break;
            case CompareMode::Unchanged:
                keep = (memcmp(currentBuf, c.snapshotValue.data(), sz) == 0);
                break;
            case CompareMode::Increased:
                keep = (compareTypedBytes(currentBuf, c.snapshotValue.data(), type) > 0);
                break;
            case CompareMode::Decreased:
                keep = (compareTypedBytes(currentBuf, c.snapshotValue.data(), type) < 0);
                break;
            case CompareMode::GreaterThan:
                if (compareValue)
                    keep = (compareTypedBytes(currentBuf, (const uint8_t *)compareValue, type) > 0);
                break;
            case CompareMode::LessThan:
                if (compareValue)
                    keep = (compareTypedBytes(currentBuf, (const uint8_t *)compareValue, type) < 0);
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
