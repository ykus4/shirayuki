#import "SYBaseHandler.h"

/// Dump tab: hex dump or ARM64 disassembly of an address range, rendered as
/// table rows rather than into an alert, so a long dump stays scrollable and
/// individual lines stay tappable.
@interface SYDumpHandler : SYBaseHandler
@end
