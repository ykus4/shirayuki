#import "SYBaseHandler.h"

/// Freeze tab: holds an address at a value, or steps it every tick.
///
/// Rows are NSMutableDictionary mirrors of the `FreezeManager` entries, kept in
/// the base class's `entries` array.
@interface SYFreezeHandler : SYBaseHandler

- (void)removeAll;
- (void)toggleAutoIncrementForRow:(NSInteger)row;

@end
