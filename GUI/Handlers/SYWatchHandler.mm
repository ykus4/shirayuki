#import "SYWatchHandler.h"

#import "SYInput.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiViewController.h"
#import "Watchpoint.hpp"

using namespace Shirayuki;

/// How often the table is redrawn while watches exist. Independent of
/// `kWatchIntervalMs`: the poll thread has to be quick enough to catch a change,
/// the UI only has to be quick enough to look live.
static const NSTimeInterval kRefreshInterval = 0.5;

static const CGFloat kCellIconSize = 14;

/// Type used when the command line omits one.
static NSString *const kDefaultTypeTag = @"int32";

@implementation SYWatchHandler {
    /// The rows the table is currently showing.
    ///
    /// `WatchManager::entries()` returns the vector by value, so calling it from
    /// `numberOfRows`, from `cellForRow:` for every visible row, and again from
    /// the selection and delete handlers copied the whole watch list a dozen
    /// times per redraw — twice a second. It is snapshotted once per reload
    /// instead, which also keeps the row count and the row contents from
    /// disagreeing when the poll thread mutates the list mid-reload.
    std::vector<WatchEntry> _snapshot;

    /// Non-nil only while there is at least one watch. See `syncRefreshTimer`.
    NSTimer *_refreshTimer;
}

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{
        @"title" : @"Watch",
        @"icon" : @"eye",
        @"placeholder" : @"0xADDR [type:i32|f32|i64]",
        @"typeLabel" : @"eye",
        @"actionIcon" : @"plus.circle.fill"
    };
}

- (void)dealloc {
    [_refreshTimer invalidate];
}

#pragma mark - Refresh timer

/// Create or tear down the refresh timer to match the watch list.
///
/// The timer used to be created in `init` and to capture `self` strongly through
/// `self.viewController`. Since `self` also owned the timer, that was a retain
/// cycle: `dealloc` never ran, the `invalidate` in it never happened, and the
/// timer kept reloading the table for the rest of the process's life — even
/// though the run loop also keeps a strong reference of its own, so the cycle
/// could never be broken from outside. It is now weak-captured, and it only
/// exists while there is something to refresh, so opening the app and never
/// visiting this tab schedules nothing at all.
- (void)syncRefreshTimer {
    const BOOL wanted = WatchManager::shared().count() > 0;
    if (wanted == (_refreshTimer != nil))
        return;

    if (!wanted) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
        return;
    }

    __weak __typeof__(self) weakSelf = self;
    void (^tick)(NSTimer *) = ^(NSTimer *timer) {
        __strong __typeof__(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) {
            // The handler is gone, and the run loop's own strong reference means
            // nothing else would ever invalidate this.
            [timer invalidate];
            return;
        }
        [strongSelf refreshTick];
    };
    _refreshTimer = [NSTimer scheduledTimerWithTimeInterval:kRefreshInterval
                                                    repeats:YES
                                                      block:tick];
}

/// Timer body. A scheduled `NSTimer` block already runs on the run loop that
/// scheduled it — the main one — so the `dispatch_async(dispatch_get_main_queue()`
/// hop this used to make was pure overhead.
- (void)refreshTick {
    if (WatchManager::shared().count() == 0) {
        [self syncRefreshTimer]; // Nothing left to watch: stop ticking.
        return;
    }
    [self.viewController reloadTable];
}

#pragma mark - Actions

- (void)performAction:(NSString *)input {
    SYCommand *command = [SYCommand parse:input minArgs:0];
    if (!command.ok) {
        [self failWithMessage:command.error];
        return;
    }

    // `tryFromString` accepts both the short tags ("f32") and the canonical ones
    // ("float"), which is why the hand-written tag translation table this used to
    // carry is gone. It also reports a typo instead of the old `?: @"int32"`,
    // which quietly watched four bytes as an int for any tag it did not know.
    NSString *typeTag = [command argAt:0 or:kDefaultTypeTag];
    ValueType type = ValueType::Int32;
    if (!SYValueTypeUtil::tryFromString(typeTag, type)) {
        [self failWithMessage:[NSString stringWithFormat:@"Unknown type '%@'", typeTag]];
        return;
    }

    auto &manager = WatchManager::shared();
    manager.add(command.address, type, "");
    if (!manager.isRunning())
        manager.start(kWatchIntervalMs);

    [self syncRefreshTimer];
    [self finishWithMessage:[NSString stringWithFormat:@"Watching %@ [%@]",
                                                       SYFormatAddress(command.address),
                                                       SYValueTypeUtil::shortLabel(type)]
                       type:SYToastSuccess];
}

- (void)removeAll {
    WatchManager::shared().removeAll();
    _snapshot.clear();
    [self syncRefreshTimer];
    [self finishWithMessage:@"All watches removed" type:SYToastInfo];
}

/// Action-button long press. `removeAll` previously had no caller anywhere, so
/// the only way to clear the watch list was to swipe rows one at a time.
- (NSArray<UIAlertAction *> *)actionButtonMenuActions {
    if (WatchManager::shared().count() == 0)
        return @[];

    __weak __typeof__(self) weakSelf = self;
    return @[
        [UIAlertAction actionWithTitle:@"Remove All Watches"
                                 style:UIAlertActionStyleDestructive
                               handler:^(UIAlertAction *a) {
                                   [weakSelf removeAll];
                               }],
    ];
}

#pragma mark - Table

- (NSInteger)numberOfRows {
    // UITableView always asks for the count before it asks for any row, so this
    // is the one place the snapshot can be refreshed such that every row the
    // table then requests is backed by the data the count came from.
    _snapshot = WatchManager::shared().entries();
    return (NSInteger)_snapshot.size();
}

/// Overrides the base implementation, which bounds-checks against
/// `numberOfRows` — calling that here would take a fresh snapshot per row and
/// reintroduce the copying this class exists to avoid.
- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell = [self dequeueCellIn:tableView row:row];
    if (row >= 0 && (size_t)row < _snapshot.size())
        [self configureCell:cell forRow:row];
    return cell;
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    const WatchEntry &entry = _snapshot[(size_t)row];
    NSString *typeLabel = SYValueTypeUtil::shortLabel(entry.type);
    NSString *current = @(ValueFormat::format(entry.currentValue.data(), entry.type).c_str());

    NSString *detail;
    if (entry.hasChanged && !entry.previousValue.empty()) {
        // Format the previous bytes straight from the entry. The old code copied
        // the entire WatchEntry — label, both value vectors and all — and
        // overwrote its current value, purely to reuse a formatter that takes an
        // entry rather than bytes.
        NSString *previous = @(ValueFormat::format(entry.previousValue.data(), entry.type).c_str());
        detail = [NSString stringWithFormat:@"%@ → %@ [%@]", previous, current, typeLabel];
    } else {
        detail = [NSString stringWithFormat:@"= %@ [%@]", current, typeLabel];
    }

    NSString *badge =
        entry.changeCount > 0 ? [NSString stringWithFormat:@"%llu", entry.changeCount] : nil;

    [cell configureWithIcon:[SYTheme icon:entry.hasChanged ? @"bolt.fill" : @"eye.fill"
                                     size:kCellIconSize
                                    color:entry.hasChanged ? [SYTheme warning] : [SYTheme success]]
                      title:SYFormatAddress(entry.address)
                     detail:detail
                      badge:badge
                 badgeColor:[SYTheme accentDim]];
}

- (void)didSelectRow:(NSInteger)row {
    if (row < 0 || (size_t)row >= _snapshot.size())
        return;
    [self copyAddressToClipboard:_snapshot[(size_t)row].address];
}

- (void)deleteRow:(NSInteger)row {
    if (row < 0 || (size_t)row >= _snapshot.size())
        return;
    WatchManager::shared().remove(_snapshot[(size_t)row].id);
    _snapshot.erase(_snapshot.begin() + row);
    [self syncRefreshTimer];
}

@end
