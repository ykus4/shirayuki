// FreezeManager / WatchManager: do they actually hold a value, detect a change,
// and start and stop cleanly?
//
// These tests own their own manager instances rather than using shared(), which
// is deliberately leaked so the singleton is never torn down inside a host app.
#include "Freeze.hpp"
#include "ShirayukiMemory.hpp"
#include "Watchpoint.hpp"
#include "syharness.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace Shirayuki;

namespace {

void waitBriefly(unsigned ms = 80) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Poll until `predicate` holds or the budget runs out, so the tests do not
// depend on an exact number of worker passes.
template <typename F> bool waitUntil(F predicate, unsigned budgetMs = 2000) {
    const unsigned step = 10;
    for (unsigned waited = 0; waited < budgetMs; waited += step) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
    }
    return predicate();
}

} // namespace

static void testFreezeHoldsValue() {
    FreezeManager fm;

    volatile int32_t target = 100;
    const int32_t frozen = 777;

    const uint64_t id = fm.add(reinterpret_cast<uintptr_t>(&target), &frozen, sizeof(frozen),
                               ValueType::Int32, "test");
    SY_CHECK(id != 0);
    SY_CHECK_EQ(fm.count(), 1u);

    fm.start(kFreezeIntervalMs);
    SY_CHECK(fm.isRunning());

    // The worker should overwrite the value we poke in.
    SY_CHECK(waitUntil([&] { return static_cast<int32_t>(target) == frozen; }));

    target = 1;
    SY_CHECK(waitUntil([&] { return static_cast<int32_t>(target) == frozen; }));

    fm.stop();
    SY_CHECK(!fm.isRunning());

    // Once stopped, the value must stay where we put it.
    target = 42;
    waitBriefly();
    SY_CHECK_EQ(static_cast<int32_t>(target), 42);
}

static void testFreezeRemoveAndPause() {
    FreezeManager fm;

    volatile int32_t target = 0;
    const int32_t frozen = 55;
    const uint64_t id = fm.add(reinterpret_cast<uintptr_t>(&target), &frozen, sizeof(frozen),
                               ValueType::Int32, "test");
    fm.start(kFreezeIntervalMs);
    SY_CHECK(waitUntil([&] { return static_cast<int32_t>(target) == frozen; }));

    // Pausing an entry must stop the writes without removing it.
    fm.setActive(id, false);
    waitBriefly();
    target = 7;
    waitBriefly();
    SY_CHECK_EQ(static_cast<int32_t>(target), 7);
    SY_CHECK_EQ(fm.count(), 1u);

    fm.setActive(id, true);
    SY_CHECK(waitUntil([&] { return static_cast<int32_t>(target) == frozen; }));

    fm.remove(id);
    SY_CHECK_EQ(fm.count(), 0u);
    waitBriefly();
    target = 9;
    waitBriefly();
    SY_CHECK_EQ(static_cast<int32_t>(target), 9);

    fm.stop();
}

// A conditional freeze compares numerically. A bytewise compare would fire on
// the wrong side of the threshold for every multi-byte type.
static void testFreezeConditionalUsesNumericOrder() {
    FreezeManager fm;

    volatile int32_t target = 1;
    const int32_t clampTo = 50;
    const int32_t threshold = 100;

    // "If the value exceeds 100, write 50."
    fm.addConditional(reinterpret_cast<uintptr_t>(&target), &clampTo, sizeof(clampTo),
                      ValueType::Int32, CompareMode::GreaterThan, &threshold, sizeof(threshold));
    fm.start(kFreezeIntervalMs);

    // 1 is below the threshold, so nothing should happen. Bytewise, 1 is
    // [01 00 00 00] and 100 is [64 00 00 00], so this case would agree — the
    // interesting one is next.
    waitBriefly();
    SY_CHECK_EQ(static_cast<int32_t>(target), 1);

    // 256 is [00 01 00 00]: its first byte is 0, below 100's 0x64, so a bytewise
    // compare reads it as "not greater" and the clamp never fires.
    target = 256;
    SY_CHECK(waitUntil([&] { return static_cast<int32_t>(target) == clampTo; }));

    fm.stop();
}

static void testWatchDetectsChange() {
    WatchManager wm;

    volatile int32_t target = 10;
    const uint64_t id = wm.add(reinterpret_cast<uintptr_t>(&target), ValueType::Int32, "watched");
    SY_CHECK(id != 0);
    SY_CHECK_EQ(wm.count(), 1u);

    std::atomic<int> callbackCount{0};
    wm.setCallback([&callbackCount](const WatchEntry &) { callbackCount++; });

    wm.start(kWatchIntervalMs);
    SY_CHECK(wm.isRunning());
    waitBriefly(200);

    target = 20;
    SY_CHECK(waitUntil([&] { return callbackCount.load() > 0; }));

    auto entries = wm.entries();
    SY_CHECK_EQ(entries.size(), 1u);
    if (entries.size() == 1) {
        SY_CHECK(entries[0].changeCount > 0);
        SY_CHECK_EQ(WatchManager::formatValue(entries[0]), std::string("20"));
    }

    wm.stop();
    SY_CHECK(!wm.isRunning());
}

// stop() must return promptly. With sleep_for it blocked for up to a full
// interval, which is why the UI could not stop the worker when a list emptied.
static void testStopIsPrompt() {
    FreezeManager fm;
    // An interval far longer than the test budget: only a condition-variable
    // wait can be interrupted faster than this.
    fm.start(3000);
    waitBriefly(50);

    const auto begin = std::chrono::steady_clock::now();
    fm.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - begin)
                             .count();
    SY_CHECK(!fm.isRunning());
    SY_CHECK(elapsed < 1000);

    WatchManager wm;
    wm.start(3000);
    waitBriefly(50);
    const auto begin2 = std::chrono::steady_clock::now();
    wm.stop();
    const auto elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - begin2)
                              .count();
    SY_CHECK(elapsed2 < 1000);
}

// Repeated start/stop must not terminate (assigning to a joinable std::thread
// does) or leave the worker running.
static void testRestartCycles() {
    FreezeManager fm;
    for (int i = 0; i < 5; i++) {
        fm.start(kFreezeIntervalMs);
        SY_CHECK(fm.isRunning());
        fm.stop();
        SY_CHECK(!fm.isRunning());
    }

    // A redundant stop, and a redundant start, must both be harmless.
    fm.stop();
    fm.start(kFreezeIntervalMs);
    fm.start(kFreezeIntervalMs);
    SY_CHECK(fm.isRunning());
    fm.stop();
    SY_CHECK(!fm.isRunning());
}

// Interval must be clamped: 0 would spin, and an unbounded value would stall
// shutdown.
static void testIntervalIsClamped() {
    FreezeManager fm;
    fm.setInterval(0);
    SY_CHECK(fm.interval() >= kMinWorkerIntervalMs);
    fm.setInterval(10 * 1000 * 1000);
    SY_CHECK(fm.interval() <= kMaxWorkerIntervalMs);
}

static void run() {
    testFreezeHoldsValue();
    testFreezeRemoveAndPause();
    testFreezeConditionalUsesNumericOrder();
    testWatchDetectsChange();
    testStopIsPrompt();
    testRestartCycles();
    testIntervalIsClamped();
}

SY_MAIN("test_workers")
