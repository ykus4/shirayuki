#ifndef SHIRAYUKI_FREEZE_HPP
#define SHIRAYUKI_FREEZE_HPP

#include "ShirayukiMemory.hpp"
#include <mutex>
#include <thread>
#include <atomic>

namespace Shirayuki {

struct FreezeEntry {
    uint64_t id;
    uintptr_t address;
    std::vector<uint8_t> value;
    std::string label;
    bool active;
};

class FreezeManager {
public:
    static FreezeManager &shared();

    // Add a freeze entry (returns ID)
    uint64_t add(uintptr_t address, const void *value, size_t len,
                 const std::string &label = "");

    // Typed freeze
    template <typename T>
    uint64_t addValue(uintptr_t address, T value, const std::string &label = "") {
        return add(address, &value, sizeof(T), label);
    }

    // Remove by ID
    void remove(uint64_t id);

    // Remove all
    void removeAll();

    // Pause/resume a single entry
    void setActive(uint64_t id, bool active);

    // Start/stop the freeze loop
    void start(uint32_t intervalMs = 16); // ~60fps default
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Set interval
    void setInterval(uint32_t ms) { m_intervalMs = ms; }

    // Get all entries (thread-safe copy)
    std::vector<FreezeEntry> entries() const;

    // Entry count
    size_t count() const;

private:
    FreezeManager() = default;
    void loop();

    mutable std::mutex m_mutex;
    std::vector<FreezeEntry> m_entries;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    uint32_t m_intervalMs = 16;
    uint64_t m_nextId = 1;
};

} // namespace Shirayuki

#endif // SHIRAYUKI_FREEZE_HPP
