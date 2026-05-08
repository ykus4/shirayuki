#include "SYScanHelper.hpp"
#include "ShirayukiMemory.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace Shirayuki;

static std::vector<uintptr_t> scanRegionCpp(uintptr_t start, size_t len, const std::string &t,
                                            const std::string &v, size_t &outValSize) {
    outValSize = 4;
    if (t == "int32") {
        int32_t val = v.empty() ? 0 : (int32_t)std::stoi(v);
        return Scanner::findValue<int32_t>(start, len, val);
    } else if (t == "int16") {
        outValSize = 2;
        int16_t val = v.empty() ? 0 : (int16_t)std::stoi(v);
        return Scanner::findValue<int16_t>(start, len, val);
    } else if (t == "int64") {
        outValSize = 8;
        int64_t val = v.empty() ? 0 : (int64_t)std::stoll(v);
        return Scanner::findValue<int64_t>(start, len, val);
    } else if (t == "float") {
        float val = v.empty() ? 0.0f : std::stof(v);
        return Scanner::findValue<float>(start, len, val);
    } else if (t == "double") {
        outValSize = 8;
        double val = v.empty() ? 0.0 : std::stod(v);
        return Scanner::findValue<double>(start, len, val);
    } else if (t == "hex") {
        outValSize = 0;
        return Scanner::findPattern(start, len, v);
    } else if (t == "regex") {
        outValSize = 0;
        return Scanner::findRegex(start, len, v);
    } else {
        outValSize = v.size();
        return Scanner::findString(start, len, v);
    }
}

static uintptr_t *vectorToHeap(const std::vector<uintptr_t> &v, size_t *outCount) {
    if (v.empty()) {
        *outCount = 0;
        return nullptr;
    }
    uintptr_t *arr = (uintptr_t *)malloc(v.size() * sizeof(uintptr_t));
    if (!arr) {
        *outCount = 0;
        return nullptr;
    }
    memcpy(arr, v.data(), v.size() * sizeof(uintptr_t));
    *outCount = v.size();
    return arr;
}

uintptr_t *SYScanAll(const char *type, const char *input, size_t maxResults, size_t maxRegionSize,
                     size_t *outCount, size_t *outValSize) {
    *outCount = 0;
    *outValSize = 4;

    std::string t(type ? type : "int32");
    std::string v(input ? input : "");

    std::vector<RegionInfo> regions = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
    std::vector<uintptr_t> allHits;
    allHits.reserve(256);

    for (size_t i = 0; i < regions.size(); i++) {
        if (allHits.size() >= maxResults)
            break;
        if (regions[i].size > maxRegionSize)
            continue;

        size_t valSize = 4;
        std::vector<uintptr_t> hits =
            scanRegionCpp(regions[i].start, regions[i].size, t, v, valSize);
        *outValSize = valSize;

        for (size_t k = 0; k < hits.size() && allHits.size() < maxResults; k++) {
            allHits.push_back(hits[k]);
        }
    }

    return vectorToHeap(allHits, outCount);
}

uintptr_t *SYScanRegion(uintptr_t start, size_t len, const char *type, const char *input,
                        size_t *outCount, size_t *outValSize) {
    std::string t(type ? type : "int32");
    std::string v(input ? input : "");
    size_t valSize = 4;
    std::vector<uintptr_t> hits = scanRegionCpp(start, len, t, v, valSize);
    *outValSize = valSize;
    return vectorToHeap(hits, outCount);
}

void SYScanFreeResults(uintptr_t *results) {
    free(results);
}

int SYMemRead(uintptr_t addr, unsigned char *buf, size_t valSize) {
    return Memory::read(addr, buf, valSize) == Status::Success ? 1 : 0;
}
