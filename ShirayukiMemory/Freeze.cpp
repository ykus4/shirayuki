#include "Freeze.hpp"
#include <chrono>

namespace Shirayuki {

FreezeManager &FreezeManager::shared() {
    static FreezeManager instance;
    return instance;
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

void FreezeManager::start(uint32_t intervalMs) {
    // Prevent double-start race
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
        return;

    m_intervalMs.store(intervalMs);
    m_stopRequested.store(false);

    m_thread = std::thread(&FreezeManager::loop, this);
}

void FreezeManager::stop() {
    m_stopRequested.store(true);
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void FreezeManager::loop() {
    while (!m_stopRequested.load()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &entry : m_entries) {
                if (!entry.active)
                    continue;

                if (entry.hasCondition) {
                    // Read current value and check condition
                    size_t sz = entry.value.size();
                    std::vector<uint8_t> current(sz);
                    if (Memory::read(entry.address, current.data(), sz) != Status::Success)
                        continue;

                    bool shouldWrite = false;
                    switch (entry.condition) {
                        case CompareMode::GreaterThan:
                            // Write when current > threshold
                            if (!entry.threshold.empty()) {
                                // Simple memcmp for now (works for int types)
                                int cmp = memcmp(current.data(), entry.threshold.data(),
                                                 std::min(sz, entry.threshold.size()));
                                shouldWrite = (cmp > 0);
                            }
                            break;
                        case CompareMode::LessThan:
                            if (!entry.threshold.empty()) {
                                int cmp = memcmp(current.data(), entry.threshold.data(),
                                                 std::min(sz, entry.threshold.size()));
                                shouldWrite = (cmp < 0);
                            }
                            break;
                        case CompareMode::Changed:
                            shouldWrite = (current != entry.value);
                            break;
                        default:
                            shouldWrite = true;
                            break;
                    }

                    if (shouldWrite) {
                        Memory::write(entry.address, entry.value.data(), entry.value.size());
                        if (entry.onTriggered) {
                            entry.onTriggered(entry.id, entry.address);
                        }
                    }
                } else {
                    // Unconditional freeze
                    Memory::write(entry.address, entry.value.data(), entry.value.size());
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs.load()));
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

FreezeEntry *FreezeManager::getEntry(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

} // namespace Shirayuki
