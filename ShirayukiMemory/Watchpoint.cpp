#include "Watchpoint.hpp"
#include <cstring>

namespace Shirayuki {

WatchManager &WatchManager::shared() {
    static WatchManager instance;
    return instance;
}

WatchManager::~WatchManager() {
    stop();
}

uint64_t WatchManager::add(uintptr_t address, ValueType type, const std::string &label) {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t sz = valueTypeSize(type);
    WatchEntry entry;
    entry.id = m_nextId++;
    entry.address = address;
    entry.type = type;
    entry.label = label;
    entry.active = true;
    entry.currentValue.resize(sz, 0);
    entry.previousValue.resize(sz, 0);
    entry.lastChangeTime = std::chrono::steady_clock::now();

    // Read initial value
    Memory::read(address, entry.currentValue.data(), sz);
    entry.previousValue = entry.currentValue;

    m_entries.push_back(entry);
    return entry.id;
}

void WatchManager::remove(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [id](const WatchEntry &e) { return e.id == id; }),
                    m_entries.end());
}

void WatchManager::removeAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

void WatchManager::setActive(uint64_t id, bool active) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_entries) {
        if (entry.id == id) {
            entry.active = active;
            break;
        }
    }
}

void WatchManager::setCallback(WatchCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = callback;
}

void WatchManager::start(uint32_t intervalMs) {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
        return;

    m_intervalMs.store(intervalMs);
    m_stopRequested.store(false);
    m_thread = std::thread(&WatchManager::loop, this);
}

void WatchManager::stop() {
    m_stopRequested.store(true);
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void WatchManager::loop() {
    while (!m_stopRequested.load()) {
        // Collect changed entries and callback outside the lock to avoid deadlock
        WatchCallback cbCopy;
        std::vector<WatchEntry> triggered;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cbCopy = m_callback;
            for (auto &entry : m_entries) {
                if (!entry.active)
                    continue;

                size_t sz = valueTypeSize(entry.type);
                std::vector<uint8_t> newVal(sz);

                if (Memory::read(entry.address, newVal.data(), sz) != Status::Success)
                    continue;

                entry.previousValue = entry.currentValue;
                entry.currentValue = newVal;

                if (entry.currentValue != entry.previousValue) {
                    entry.hasChanged = true;
                    entry.changeCount++;
                    entry.lastChangeTime = std::chrono::steady_clock::now();
                    if (cbCopy) {
                        triggered.push_back(entry);
                    }
                } else {
                    entry.hasChanged = false;
                }
            }
        }

        // Invoke callbacks after releasing lock
        for (auto &e : triggered) {
            cbCopy(e);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs.load()));
    }
}

std::vector<WatchEntry> WatchManager::entries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

size_t WatchManager::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

} // namespace Shirayuki
