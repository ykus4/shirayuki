#ifndef SHIRAYUKI_WATCHPOINT_HPP
#define SHIRAYUKI_WATCHPOINT_HPP

#include "ShirayukiMemory.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace Shirayuki {

struct WatchEntry {
    uint64_t id;
    uintptr_t address;
    ValueType type;
    std::string label;
    bool active;

    // Current and previous values
    std::vector<uint8_t> currentValue;
    std::vector<uint8_t> previousValue;
    bool hasChanged = false;

    // Stats
    uint64_t changeCount = 0;
    std::chrono::steady_clock::time_point lastChangeTime;

    // --- Conditional trigger (Item #25, skeleton) ---
    // When `hasTrigger` is true, the watch loop evaluates `condition` against
    // (currentValue, threshold) each tick. If it matches, `onTriggered` fires
    // (executed outside the manager lock). Set `oneShot` to auto-disable after
    // the first firing. Real UI wiring is TODO — API is stable so callers can
    // start binding triggers programmatically today.
    bool hasTrigger = false;
    CompareMode condition = CompareMode::Exact;
    std::vector<uint8_t> threshold;
    bool oneShot = false;
    std::function<void(const WatchEntry &)> onTriggered;
};

// Callback when a watch value changes
using WatchCallback = std::function<void(const WatchEntry &entry)>;

class WatchManager {
  public:
    static WatchManager &shared();
    ~WatchManager();

    // Add a watchpoint
    uint64_t add(uintptr_t address, ValueType type, const std::string &label = "");

    // Attach a conditional trigger to an existing watchpoint. Passing `threshold=nullptr`
    // with a compare mode of Changed/Unchanged uses the previousValue as the reference.
    // Skeleton — the manager evaluates and fires; caller supplies `callback`.
    void setTrigger(uint64_t id, CompareMode condition, const void *threshold, size_t thresholdLen,
                    bool oneShot, std::function<void(const WatchEntry &)> callback);
    void clearTrigger(uint64_t id);

    // Remove
    void remove(uint64_t id);
    void removeAll();

    // Pause/resume
    void setActive(uint64_t id, bool active);

    // Set change callback
    void setCallback(WatchCallback callback);

    // Start/stop polling
    void start(uint32_t intervalMs = 100);
    void stop();
    bool isRunning() const {
        return m_running.load();
    }

    // Get entries
    std::vector<WatchEntry> entries() const;
    size_t count() const;

    // Read current value as string for display
    static std::string formatValue(const WatchEntry &entry) {
        return ValueFormat::format(entry.currentValue.data(), entry.type);
    }

  private:
    WatchManager() = default;
    void loop();

    mutable std::mutex m_mutex;
    std::vector<WatchEntry> m_entries;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    std::atomic<uint32_t> m_intervalMs{100};
    uint64_t m_nextId = 1;
    WatchCallback m_callback;
};

} // namespace Shirayuki

#endif // SHIRAYUKI_WATCHPOINT_HPP
