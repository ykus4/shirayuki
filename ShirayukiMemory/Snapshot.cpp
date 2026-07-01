#include "Snapshot.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace Shirayuki {

MemorySnapshot SnapshotManager::capture(uintptr_t start, size_t len, const std::string &label) {
    MemorySnapshot snap;
    snap.label = label;
    snap.start = start;
    if (!len)
        return snap;

    snap.bytes.resize(len);
    if (Memory::read(start, snap.bytes.data(), len) != Status::Success) {
        snap.bytes.clear();
    }
    return snap;
}

bool SnapshotManager::save(const MemorySnapshot &snap, const std::string &basePath) {
    if (snap.bytes.empty())
        return false;

    std::ofstream bin(basePath + ".bin", std::ios::binary);
    if (!bin.is_open())
        return false;
    bin.write(reinterpret_cast<const char *>(snap.bytes.data()),
              static_cast<std::streamsize>(snap.bytes.size()));

    std::ofstream meta(basePath + ".meta");
    if (!meta.is_open())
        return false;
    meta << snap.start << '\n' << snap.bytes.size() << '\n' << snap.label << '\n';
    return true;
}

bool SnapshotManager::load(const std::string &basePath, MemorySnapshot &out) {
    std::ifstream meta(basePath + ".meta");
    if (!meta.is_open())
        return false;

    size_t byteCount = 0;
    meta >> out.start >> byteCount;
    meta.ignore();
    std::getline(meta, out.label);

    std::ifstream bin(basePath + ".bin", std::ios::binary);
    if (!bin.is_open())
        return false;
    out.bytes.resize(byteCount);
    bin.read(reinterpret_cast<char *>(out.bytes.data()), static_cast<std::streamsize>(byteCount));
    return bin.gcount() == static_cast<std::streamsize>(byteCount);
}

std::vector<SnapshotDiff> SnapshotManager::diff(const MemorySnapshot &before,
                                                const MemorySnapshot &after, size_t maxDiffs) {
    std::vector<SnapshotDiff> out;
    if (before.start != after.start)
        return out; // ranges must line up
    size_t n = std::min(before.bytes.size(), after.bytes.size());

    for (size_t i = 0; i < n && out.size() < maxDiffs; i++) {
        if (before.bytes[i] != after.bytes[i]) {
            SnapshotDiff d{before.start + i, before.bytes[i], after.bytes[i]};
            out.push_back(d);
        }
    }
    return out;
}

} // namespace Shirayuki
