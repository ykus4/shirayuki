#ifndef SHIRAYUKI_SPEEDHACK_HPP
#define SHIRAYUKI_SPEEDHACK_HPP

#include <cstdint>

namespace Shirayuki {

// Speedhack — scales the effective flow of time by intercepting time sources.
//
// Skeleton only. The runtime hook install path is not implemented yet because
// it needs to be verified against Substrate/Substitute on-device (fishhook +
// interposing on `mach_absolute_time`, `gettimeofday`, `clock_gettime`,
// `CACurrentMediaTime`; NSDate/CFAbsoluteTimeGetCurrent optionally). The public
// API is designed so callers can adopt it now; the actual interposition lives
// behind `install()` when it lands.

namespace Speedhack {

// Set the multiplier: 1.0 = normal, 2.0 = twice as fast, 0.5 = half speed.
// Non-positive or NaN values are clamped to 1.0.
void setScale(double scale);
double scale();

// Install / remove the time-source interpositions. `install()` is idempotent.
// TODO(device): implement using fishhook against the mach/POSIX time symbols.
bool install();
void uninstall();
bool isInstalled();

} // namespace Speedhack
} // namespace Shirayuki

#endif // SHIRAYUKI_SPEEDHACK_HPP
