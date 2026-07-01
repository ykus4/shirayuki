#include "SYScanHelper.hpp"
#include "ShirayukiMemory.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace Shirayuki;

// Typed value scan dispatched by ValueType. Each branch reads the parsed bytes
// as the concrete integer/float type so findValue does a bitwise-exact match.
static std::vector<uintptr_t> scanTypedValue(uintptr_t start, size_t len, ValueType type,
                                             const uint8_t *bytes) {
    switch (type) {
        case ValueType::Int8: {
            int8_t v;
            memcpy(&v, bytes, 1);
            return Scanner::findValue<int8_t>(start, len, v);
        }
        case ValueType::UInt8: {
            uint8_t v;
            memcpy(&v, bytes, 1);
            return Scanner::findValue<uint8_t>(start, len, v);
        }
        case ValueType::Int16: {
            int16_t v;
            memcpy(&v, bytes, 2);
            return Scanner::findValue<int16_t>(start, len, v);
        }
        case ValueType::UInt16: {
            uint16_t v;
            memcpy(&v, bytes, 2);
            return Scanner::findValue<uint16_t>(start, len, v);
        }
        case ValueType::Int32: {
            int32_t v;
            memcpy(&v, bytes, 4);
            return Scanner::findValue<int32_t>(start, len, v);
        }
        case ValueType::UInt32: {
            uint32_t v;
            memcpy(&v, bytes, 4);
            return Scanner::findValue<uint32_t>(start, len, v);
        }
        case ValueType::Int64: {
            int64_t v;
            memcpy(&v, bytes, 8);
            return Scanner::findValue<int64_t>(start, len, v);
        }
        case ValueType::UInt64: {
            uint64_t v;
            memcpy(&v, bytes, 8);
            return Scanner::findValue<uint64_t>(start, len, v);
        }
        case ValueType::Float32: {
            float v;
            memcpy(&v, bytes, 4);
            return Scanner::findValue<float>(start, len, v);
        }
        case ValueType::Float64: {
            double v;
            memcpy(&v, bytes, 8);
            return Scanner::findValue<double>(start, len, v);
        }
    }
    return {};
}

static std::vector<uintptr_t> scanRegionCpp(uintptr_t start, size_t len, const std::string &t,
                                            const std::string &v, size_t &outValSize) {
    if (t == "hex") {
        outValSize = 0;
        return Scanner::findPattern(start, len, v);
    }
    if (t == "regex") {
        outValSize = 0;
        return Scanner::findRegex(start, len, v);
    }
    if (t == "string") {
        outValSize = v.size();
        return Scanner::findString(start, len, v);
    }

    // Numeric type: parse the input using ValueFormat, then dispatch to Scanner::findValue<T>.
    ValueType vt = ValueFormat::fromTag(t);
    outValSize = valueTypeSize(vt);
    uint8_t buf[8] = {};
    if (!ValueFormat::parse(v, vt, buf))
        return {};
    return scanTypedValue(start, len, vt, buf);
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
