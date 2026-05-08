#ifndef SHIRAYUKI_FREEZE_HPP
#define SHIRAYUKI_FREEZE_HPP

#include "ShirayukiMemory.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace Shirayuki {

struct FreezeEntry {
    uint64_t id;
    uintptr_t address;
    std::vector<uint8_t> value;
    ValueType type;
    std::string label;
    bool active;

    // Conditional freeze
    bool hasCondition = false;
    CompareMode condition = CompareMode::Exact;
    std::vector<uint8_t> threshold;
    std::function<void(uint64_t id, uintptr_t addr)> onTriggered;

    // Auto-increment: each tick adds incrementStep to the current memory value
    bool autoIncrement = false;
    int64_t incrementStep = 1;
};

class FreezeManager {
  public:
    static FreezeManager &shared();
    ~FreezeManager();

    // Add a freeze entry (returns ID)
    uint64_t add(uintptr_t address, const void *value, size_t len,
                 ValueType type = ValueType::Int32, const std::string &label = "");

    // Typed freeze
    template <typename T>
    uint64_t addValue(uintptr_t address, T value, const std::string &label = "") {
        ValueType vt = ValueType::Int32;
        if constexpr (std::is_same_v<T, float>)
            vt = ValueType::Float32;
        else if constexpr (std::is_same_v<T, double>)
            vt = ValueType::Float64;
        else if constexpr (std::is_same_v<T, int64_t>)
            vt = ValueType::Int64;
        else if constexpr (std::is_same_v<T, int16_t>)
            vt = ValueType::Int16;
        else if constexpr (std::is_same_v<T, int8_t>)
            vt = ValueType::Int8;
        return add(address, &value, sizeof(T), vt, label);
    }

    // Conditional freeze: write only when condition is met
    uint64_t addConditional(uintptr_t address, const void *value, size_t len, ValueType type,
                            CompareMode condition, const void *threshold, size_t thresholdLen,
                            std::function<void(uint64_t, uintptr_t)> callback = nullptr);

    // Remove by ID
    void remove(uint64_t id);
    void removeAll();

    // Pause/resume a single entry
    void setActive(uint64_t id, bool active);

    // Update value for existing entry
    void updateValue(uint64_t id, const void *value, size_t len);

    // Enable auto-increment on an entry
    void setAutoIncrement(uint64_t id, bool enabled, int64_t step = 1);

    // Start/stop the freeze loop
    void start(uint32_t intervalMs = 16);
    void stop();
    bool isRunning() const {
        return m_running.load();
    }

    // Set interval
    void setInterval(uint32_t ms) {
        m_intervalMs.store(ms);
    }
    uint32_t interval() const {
        return m_intervalMs.load();
    }

    // Get all entries (thread-safe copy)
    std::vector<FreezeEntry> entries() const;
    size_t count() const;

    // Get single entry by value (thread-safe copy)
    std::optional<FreezeEntry> getEntry(uint64_t id) const;

  private:
    FreezeManager() = default;
    void loop();

    mutable std::mutex m_mutex;
    std::vector<FreezeEntry> m_entries;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    std::atomic<uint32_t> m_intervalMs{16};
    uint64_t m_nextId = 1;
};

} // namespace Shirayuki

#endif // SHIRAYUKI_FREEZE_HPP
