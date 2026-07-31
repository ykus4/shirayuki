#include "Freeze.hpp"
#include <chrono>
#include <optional>

namespace Shirayuki {

FreezeManager &FreezeManager::shared() {
    // Deliberately leaked. A function-local static would be destroyed via
    // atexit, and this code lives in a dylib injected into a host app: joining
    // a worker thread during process teardown is a known way to deadlock, and
    // the entries are worthless at that point anyway. The destructor remains for
    // tests, which own their instances explicitly.
    static FreezeManager *instance = new FreezeManager();
    return *instance;
}

FreezeManager::~FreezeManager() {
    stop();
}

uint64_t FreezeManager::add(uintptr_t address, const void *value, size_t len, ValueType type,
                            const std::string &label) {
    std::lock_guard<std::mutex> lock(m_mutex);

    FreezeEntry entry;
    entry.id = m_nextId++;
    entry.address = address;
    entry.value.assign(reinterpret_cast<const uint8_t *>(value),
                       reinterpret_cast<const uint8_t *>(value) + len);
    entry.type = type;
    entry.label = label;
    entry.active = true;

    m_entries.push_back(entry);
    return entry.id;
}

uint64_t FreezeManager::addConditional(uintptr_t address, const void *value, size_t len,
                                       ValueType type, CompareMode condition, const void *threshold,
                                       size_t thresholdLen,
                                       std::function<void(uint64_t, uintptr_t)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);

    FreezeEntry entry;
    entry.id = m_nextId++;
    entry.address = address;
    entry.value.assign(reinterpret_cast<const uint8_t *>(value),
                       reinterpret_cast<const uint8_t *>(value) + len);
    entry.type = type;
    entry.active = true;
    entry.hasCondition = true;
    entry.condition = condition;
    if (threshold && thresholdLen > 0) {
        entry.threshold.assign(reinterpret_cast<const uint8_t *>(threshold),
                               reinterpret_cast<const uint8_t *>(threshold) + thresholdLen);
    }
    entry.onTriggered = callback;

    m_entries.push_back(entry);
    return entry.id;
}

void FreezeManager::remove(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [id](const FreezeEntry &e) { return e.id == id; }),
                    m_entries.end());
}

void FreezeManager::removeAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

void FreezeManager::setActive(uint64_t id, bool active) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_entries) {
        if (entry.id == id) {
            entry.active = active;
            break;
        }
    }
}

void FreezeManager::updateValue(uint64_t id, const void *value, size_t len) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_entries) {
        if (entry.id == id) {
            entry.value.assign(reinterpret_cast<const uint8_t *>(value),
                               reinterpret_cast<const uint8_t *>(value) + len);
            break;
        }
    }
}

void FreezeManager::setAutoIncrement(uint64_t id, bool enabled, int64_t step) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_entries) {
        if (entry.id == id) {
            entry.autoIncrement = enabled;
            entry.incrementStep = step;
            break;
        }
    }
}

void FreezeManager::setInterval(uint32_t ms) {
    if (ms < kMinWorkerIntervalMs)
        ms = kMinWorkerIntervalMs;
    if (ms > kMaxWorkerIntervalMs)
        ms = kMaxWorkerIntervalMs;
    m_intervalMs.store(ms);
    // Apply immediately rather than after the current wait expires.
    m_wakeup.notify_all();
}

void FreezeManager::start(uint32_t intervalMs) {
    // Serialise against stop(): assigning to a std::thread that is still
    // joinable calls std::terminate, and the CAS below cannot prevent a start
    // that races a stop still inside its join().
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);

    setInterval(intervalMs);

    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
        return;

    // A previous worker may have exited without being joined yet.
    if (m_thread.joinable())
        m_thread.join();

    m_stopRequested.store(false);
    m_thread = std::thread(&FreezeManager::loop, this);
}

void FreezeManager::stop() {
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);

    m_stopRequested.store(true);
    m_running.store(false);
    // Wake the worker out of its wait so the join below does not have to sit
    // through the rest of the poll interval.
    m_wakeup.notify_all();

    if (m_thread.joinable())
        m_thread.join();
}

void FreezeManager::loop() {
    while (!m_stopRequested.load()) {
        // Collect triggered callbacks outside the lock to avoid deadlock
        std::vector<
            std::pair<std::function<void(uint64_t, uintptr_t)>, std::pair<uint64_t, uintptr_t>>>
            pendingCallbacks;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &entry : m_entries) {
                if (!entry.active)
                    continue;

                if (entry.hasCondition) {
                    size_t sz = entry.value.size();
                    std::vector<uint8_t> current(sz);
                    if (Memory::read(entry.address, current.data(), sz) != Status::Success)
                        continue;

                    // Threshold comparisons go through compareValues, which orders
                    // numerically. memcmp compares the low byte first on
                    // little-endian ARM64, so a bytewise ">" is wrong for every
                    // multi-byte type and for all signed and float values.
                    const size_t typeWidth = valueTypeSize(entry.type);
                    const bool thresholdUsable =
                        entry.threshold.size() >= typeWidth && sz >= typeWidth;

                    bool shouldWrite = false;
                    switch (entry.condition) {
                        case CompareMode::GreaterThan:
                            shouldWrite = thresholdUsable &&
                                          compareValues(current.data(), entry.threshold.data(),
                                                        entry.type) > 0;
                            break;
                        case CompareMode::LessThan:
                            shouldWrite = thresholdUsable &&
                                          compareValues(current.data(), entry.threshold.data(),
                                                        entry.type) < 0;
                            break;
                        case CompareMode::Unchanged:
                            shouldWrite = (current == entry.value);
                            break;
                        case CompareMode::Changed:
                            shouldWrite = (current != entry.value);
                            break;
                        case CompareMode::Exact:
                        case CompareMode::Increased:
                        case CompareMode::Decreased:
                            // Not meaningful as a freeze trigger: there is no
                            // prior sample to compare against here.
                            shouldWrite = true;
                            break;
                    }

                    if (shouldWrite) {
                        Memory::write(entry.address, entry.value.data(), entry.value.size());
                        if (entry.onTriggered) {
                            pendingCallbacks.push_back(
                                {entry.onTriggered, {entry.id, entry.address}});
                        }
                    }
                } else if (entry.autoIncrement) {
                    // Read current value, add step, write back and update stored value
                    size_t sz = entry.value.size();
                    std::vector<uint8_t> current(sz);
                    if (Memory::read(entry.address, current.data(), sz) == Status::Success) {
                        // Perform signed integer addition on raw bytes (little-endian)
                        int64_t step = entry.incrementStep;
                        if (sz == 1) {
                            int8_t v;
                            memcpy(&v, current.data(), 1);
                            v += (int8_t)step;
                            memcpy(entry.value.data(), &v, 1);
                        } else if (sz == 2) {
                            int16_t v;
                            memcpy(&v, current.data(), 2);
                            v += (int16_t)step;
                            memcpy(entry.value.data(), &v, 2);
                        } else if (sz == 4) {
                            int32_t v;
                            memcpy(&v, current.data(), 4);
                            v += (int32_t)step;
                            memcpy(entry.value.data(), &v, 4);
                        } else if (sz == 8) {
                            int64_t v;
                            memcpy(&v, current.data(), 8);
                            v += step;
                            memcpy(entry.value.data(), &v, 8);
                        }
                        Memory::write(entry.address, entry.value.data(), sz);
                    }
                } else {
                    Memory::write(entry.address, entry.value.data(), entry.value.size());
                }
            }
        }

        // Invoke callbacks after releasing lock
        for (auto &[cb, args] : pendingCallbacks) {
            cb(args.first, args.second);
        }

        // Wait on the stop flag rather than sleeping, so stop() and setInterval()
        // take effect immediately instead of after the current interval elapses.
        {
            std::unique_lock<std::mutex> wakeLock(m_wakeupMutex);
            m_wakeup.wait_for(wakeLock, std::chrono::milliseconds(m_intervalMs.load()),
                              [this] { return m_stopRequested.load(); });
        }
    }
}

std::vector<FreezeEntry> FreezeManager::entries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

size_t FreezeManager::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

std::optional<FreezeEntry> FreezeManager::getEntry(uint64_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : m_entries) {
        if (entry.id == id)
            return entry;
    }
    return std::nullopt;
}

} // namespace Shirayuki
