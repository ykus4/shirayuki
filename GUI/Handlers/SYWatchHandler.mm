#import "SYWatchHandler.h"

#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiViewController.h"
#import "Watchpoint.hpp"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

static const NSTimeInterval kRefreshInterval = 0.5;

@interface SYWatchHandler ()
@property (nonatomic, strong) NSTimer *refreshTimer;
@end

@implementation SYWatchHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        // The timer captures self weakly. Capturing it strongly created a cycle
        // — self owns the timer, the run loop owns the timer, and the block owned
        // self — so dealloc never ran, the invalidate below never executed, and
        // the 500 ms refresh kept firing for the life of the process.
        __weak __typeof__(self) weakSelf = self;
        _refreshTimer = [NSTimer
            scheduledTimerWithTimeInterval:kRefreshInterval
                                   repeats:YES
                                     block:^(NSTimer *timer) {
                                         __strong __typeof__(weakSelf) strongSelf = weakSelf;
                                         if (!strongSelf) {
                                             [timer invalidate];
                                             return;
                                         }
                                         // NSTimer block callbacks already run on
                                         // the scheduling run loop, which is main.
                                         if (WatchManager::shared().count() > 0)
                                             [strongSelf.viewController reloadTable];
                                     }];
    }
    return self;
}

- (void)dealloc {
    [_refreshTimer invalidate];
}

- (NSString *)tabTitle {
    return @"Watch";
}
- (NSString *)tabIcon {
    return @"eye";
}
- (NSString *)placeholder {
    return @"0xADDR [type:i32|f32|i64]";
}
- (NSString *)typeLabel {
    return @"eye";
}
- (NSString *)actionIcon {
    return @"plus.circle.fill";
}

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    if (!addr) {
        [SYToast show:@"Invalid address" type:SYToastError];
        return;
    }

    // Same dictionary removal as the freeze tab. Note the two copies had drifted:
    // this one listed "i8" and the freeze one did not, and their fallbacks
    // differed, so the same input could mean different things on the two tabs.
    NSString *typeTag = parts.count > 1 ? parts[1] : @"i32";
    ValueType type = ValueType::Int32;
    if (!SYValueTypeUtil::tryFromString(typeTag, type)) {
        [SYToast show:[NSString stringWithFormat:@"Unknown type: %@", typeTag] type:SYToastError];
        return;
    }

    auto &wm = WatchManager::shared();
    wm.add((uintptr_t)addr, type, "");
    if (!wm.isRunning())
        wm.start(kWatchIntervalMs);

    [SYToast show:@"Watchpoint added" type:SYToastSuccess];
    [self.viewController reloadTable];
}

- (void)removeAll {
    WatchManager::shared().removeAll();
    [SYToast show:@"All watches removed" type:SYToastInfo];
    [self.viewController reloadTable];
}

- (NSInteger)numberOfRows {
    return WatchManager::shared().count();
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    auto entries = WatchManager::shared().entries();
    if (row >= (NSInteger)entries.size())
        return cell;

    auto &entry = entries[row];
    NSString *valueStr = @(WatchManager::formatValue(entry).c_str());
    NSString *addrStr = [NSString stringWithFormat:@"0x%lX", entry.address];

    UIColor *iconColor = entry.hasChanged ? [SYTheme warning] : [SYTheme success];
    NSString *badge = [NSString stringWithFormat:@"%llu", entry.changeCount];

    // Show diff line if value has changed: "prev → current [type]"
    NSString *detail;
    if (entry.hasChanged && !entry.previousValue.empty()) {
        // Format previous value using same formatting path
        WatchEntry prevCopy = entry;
        prevCopy.currentValue = entry.previousValue;
        NSString *prevStr = @(WatchManager::formatValue(prevCopy).c_str());
        detail = [NSString stringWithFormat:@"%@ → %@ [%s]", prevStr, valueStr,
                                            valueTypeLabel(entry.type).c_str()];
    } else {
        detail =
            [NSString stringWithFormat:@"= %@ [%s]", valueStr, valueTypeLabel(entry.type).c_str()];
    }

    [cell configureWithIcon:[SYTheme icon:entry.hasChanged ? @"bolt.fill" : @"eye.fill"
                                     size:14
                                    color:iconColor]
                      title:addrStr
                     detail:detail
                      badge:entry.changeCount > 0 ? badge : nil
                 badgeColor:[SYTheme accentDim]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    auto entries = WatchManager::shared().entries();
    if (row >= (NSInteger)entries.size())
        return;

    uintptr_t addr = entries[row].address;
    [UIPasteboard generalPasteboard].string = [NSString stringWithFormat:@"0x%lX", addr];
    [SYToast show:@"Address copied" type:SYToastInfo];
}

- (BOOL)canDeleteRow:(NSInteger)row {
    return YES;
}
- (void)deleteRow:(NSInteger)row {
    auto entries = WatchManager::shared().entries();
    if (row < (NSInteger)entries.size()) {
        WatchManager::shared().remove(entries[row].id);
    }
}

@end
