#include "ReadCache.hpp"
#include <chrono>
#include <cstring>

namespace Shirayuki {

uint64_t ReadCache::nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

Status ReadCache::read(uintptr_t address, void *out, size_t len) {
    if (!m_ttlMs)
        return Memory::read(address, out, len);

    uint64_t now = nowMs();

    // Linear scan is fine — Freeze/Watch have O(entries) items and each poll
    // touches them all anyway.
    for (auto &kv : m_entries) {
        if (kv.first != address)
            continue;
        Entry &e = kv.second;
        if (e.bytes.size() == len && (now - e.timestampMs) <= m_ttlMs) {
            memcpy(out, e.bytes.data(), len);
            return Status::Success;
        }
        // Same address but different length or stale — refill this slot.
        Status s = Memory::read(address, out, len);
        if (s == Status::Success) {
            e.bytes.assign(reinterpret_cast<uint8_t *>(out),
                           reinterpret_cast<uint8_t *>(out) + len);
            e.timestampMs = now;
        }
        return s;
    }

    Status s = Memory::read(address, out, len);
    if (s == Status::Success) {
        Entry e;
        e.bytes.assign(reinterpret_cast<uint8_t *>(out), reinterpret_cast<uint8_t *>(out) + len);
        e.timestampMs = now;
        m_entries.push_back({address, std::move(e)});
    }
    return s;
}

void ReadCache::invalidate(uintptr_t address) {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->first == address) {
            m_entries.erase(it);
            return;
        }
    }
}

void ReadCache::clear() {
    m_entries.clear();
}

} // namespace Shirayuki
