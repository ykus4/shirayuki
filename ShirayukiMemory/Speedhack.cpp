#include "Speedhack.hpp"
#include <atomic>
#include <cmath>

namespace Shirayuki {
namespace Speedhack {

// Atomic so the poll and hook paths can read this without a lock. The eventual
// hook implementations will multiply their result by `g_scale` on the fast path.
static std::atomic<double> g_scale{1.0};
static std::atomic<bool> g_installed{false};

void setScale(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0)
        scale = 1.0;
    g_scale.store(scale);
}

double scale() {
    return g_scale.load();
}

bool install() {
    // TODO(device): interpose mach_absolute_time / gettimeofday / clock_gettime /
    // CACurrentMediaTime and scale their return values by `g_scale`. Skeleton only.
    g_installed.store(true);
    return true;
}

void uninstall() {
    // TODO(device): undo the interposition set up by install().
    g_installed.store(false);
    g_scale.store(1.0);
}

bool isInstalled() {
    return g_installed.load();
}

} // namespace Speedhack
} // namespace Shirayuki
