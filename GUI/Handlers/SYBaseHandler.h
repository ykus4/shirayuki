#import "SYTabHandler.h"
#import "SYToast.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;
@class SYResultCell;

NS_ASSUME_NONNULL_BEGIN

/// Common base for the tab handlers.
///
/// Collects the boilerplate every handler used to repeat: the `viewController`
/// property, the entry array, cell dequeuing, the `kCellID` string, delete
/// support, address copying, and the background-work sandwich. Handlers override
/// `performAction:` and `configureCell:forRow:` plus whichever hooks they need.
///
/// Tab metadata is declarative — a subclass returns constants from
/// `+tabDescriptor` instead of implementing five one-line getters.
@interface SYBaseHandler : NSObject <SYTabHandler>

@property (nonatomic, weak) ShirayukiViewController *viewController;

/// Rows backing the table. Subclasses that keep their state elsewhere (the
/// freeze and watch tabs read it from their C++ manager) override
/// `numberOfRows` instead of using this.
@property (nonatomic, strong, readonly) NSMutableArray *entries;

/// YES while a background operation started by `runInBackground:` is in flight.
/// Guards against a second scan being launched by a double tap.
@property (nonatomic, readonly) BOOL busy;

#pragma mark - Subclass hooks

/// Static tab metadata. Keys: `title`, `icon`, `placeholder`, `typeLabel`,
/// `actionIcon` — all NSString. Subclasses whose placeholder or action icon
/// depends on state override the corresponding SYTabHandler getter directly.
+ (NSDictionary<NSString *, NSString *> *)tabDescriptor;

/// Configure a dequeued cell for `row`. Called by `cellForRow:inTableView:`.
- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row;

#pragma mark - Helpers for subclasses

/// Dequeue the shared result cell. Removes six copies of the same three lines,
/// and the six separate `kCellID` constants that had to agree with the
/// registration in ShirayukiViewController.
- (SYResultCell *)dequeueCellIn:(UITableView *)tableView row:(NSInteger)row;

/// Run `work` off the main thread, then `completion` back on it. Refuses to
/// start if a previous call is still running, and always clears `busy`.
/// Captures self weakly, so a handler torn down mid-scan is not kept alive.
- (void)runInBackground:(void (^)(void))work completion:(nullable void (^)(void))completion;

/// Copy an address to the clipboard and confirm it. Replaces four near-identical
/// copies that disagreed about the format string and the toast text.
- (void)copyAddressToClipboard:(uintptr_t)address;

/// Show `message` and reload the table. The tail of nearly every action.
- (void)finishWithMessage:(NSString *)message type:(SYToastType)type;

/// Report a parse or validation failure and stop.
- (void)failWithMessage:(NSString *)message;

@end

NS_ASSUME_NONNULL_END
