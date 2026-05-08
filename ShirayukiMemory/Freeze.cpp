#include "Freeze.hpp"
#include <chrono>

namespace Shirayuki {

FreezeManager &FreezeManager::shared() {
    static FreezeManager instance;
    return instance;
}

uint64_t FreezeManager::add(uintptr_t address, const void *value, size_t len,
                            const std::string &label) {
    std::lock_guard<std::mutex> lock(m_mutex);

    FreezeEntry entry;
    entry.id = m_nextId++;
    entry.address = address;
    entry.value.assign(
        reinterpret_cast<const uint8_t *>(value),
        reinterpret_cast<const uint8_t *>(value) + len
    );
    entry.label = label;
    entry.active = true;

    m_entries.push_back(entry);
    return entry.id;
}

void FreezeManager::remove(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [id](const FreezeEntry &e) { return e.id == id; }),
        m_entries.end()
    );
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

void FreezeManager::start(uint32_t intervalMs) {
    if (m_running.load()) return;

    m_intervalMs = intervalMs;
    m_running.store(true);
    m_thread = std::thread(&FreezeManager::loop, this);
}

void FreezeManager::stop() {
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void FreezeManager::loop() {
    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &entry : m_entries) {
                if (entry.active) {
                    Memory::write(entry.address, entry.value.data(), entry.value.size());
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
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

} // namespace Shirayuki
