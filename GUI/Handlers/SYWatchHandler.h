#import "SYBaseHandler.h"

/// Watch tab: polls addresses in the background and shows each one's
/// previous → current transition.
///
/// Rows live in `WatchManager`, not in the base class's `entries` array, so this
/// handler snapshots the manager once per table reload.
@interface SYWatchHandler : SYBaseHandler

- (void)removeAll;

@end
