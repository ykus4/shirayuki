#include "SYScanHelper.hpp"
#include "ShirayukiMemory.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace Shirayuki;

uintptr_t *SYScanRegion(uintptr_t start, size_t len, const char *type, const char *input,
                        size_t *outCount, size_t *outValSize) {
    *outCount = 0;
    *outValSize = 4;

    std::string t(type ? type : "");
    std::string v(input ? input : "");

    std::vector<uintptr_t> hits;

    if (t == "int32") {
        hits = Scanner::findValue<int32_t>(start, len, (int32_t)std::stoi(v.empty() ? "0" : v));
        *outValSize = 4;
    } else if (t == "int16") {
        hits = Scanner::findValue<int16_t>(start, len, (int16_t)std::stoi(v.empty() ? "0" : v));
        *outValSize = 2;
    } else if (t == "int64") {
        hits = Scanner::findValue<int64_t>(start, len, (int64_t)std::stoll(v.empty() ? "0" : v));
        *outValSize = 8;
    } else if (t == "float") {
        hits = Scanner::findValue<float>(start, len, std::stof(v.empty() ? "0" : v));
        *outValSize = 4;
    } else if (t == "double") {
        hits = Scanner::findValue<double>(start, len, std::stod(v.empty() ? "0" : v));
        *outValSize = 8;
    } else if (t == "hex") {
        hits = Scanner::findPattern(start, len, v);
        *outValSize = 0;
    } else if (t == "regex") {
        hits = Scanner::findRegex(start, len, v);
        *outValSize = 0;
    } else {
        // string
        hits = Scanner::findString(start, len, v);
        *outValSize = v.size();
    }

    if (hits.empty())
        return nullptr;

    uintptr_t *arr = (uintptr_t *)malloc(hits.size() * sizeof(uintptr_t));
    if (!arr)
        return nullptr;
    memcpy(arr, hits.data(), hits.size() * sizeof(uintptr_t));
    *outCount = hits.size();
    return arr;
}

void SYScanFreeResults(uintptr_t *results) {
    free(results);
}

int SYMemRead(uintptr_t addr, unsigned char *buf, size_t valSize) {
    return Memory::read(addr, buf, valSize) == Status::Success ? 1 : 0;
}
