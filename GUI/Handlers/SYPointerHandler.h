#import "SYBaseHandler.h"

/// Pointer tab: finds `module+0xN -> [off] -> [off]` chains that resolve to a
/// target address, so a heap address found by the search tab can be re-reached
/// after the app restarts.
///
/// The scan is the most expensive operation in the toolkit, so it runs through
/// `-runInBackground:completion:`, which single-flights it.
@interface SYPointerHandler : SYBaseHandler
@end
