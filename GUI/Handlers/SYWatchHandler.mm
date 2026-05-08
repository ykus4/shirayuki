#import "SYWatchHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiViewController.h"
#import "Watchpoint.hpp"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYWatchHandler ()
@property (nonatomic, strong) NSTimer *refreshTimer;
@end

@implementation SYWatchHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        // Auto-refresh table every 500ms when watch is active
        _refreshTimer =
            [NSTimer scheduledTimerWithTimeInterval:0.5
                                            repeats:YES
                                              block:^(NSTimer *t) {
                                                  if (WatchManager::shared().count() > 0) {
                                                      dispatch_async(dispatch_get_main_queue(), ^{
                                                          [self.viewController reloadTable];
                                                      });
                                                  }
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

    NSDictionary *typeMap = @{
        @"f32" : @"float",
        @"f64" : @"double",
        @"i64" : @"int64",
        @"i32" : @"int32",
        @"i16" : @"int16",
        @"i8" : @"int8"
    };
    NSString *typeTag = parts.count > 1 ? parts[1] : @"i32";
    NSString *canonicalType = typeMap[typeTag] ?: @"int32";
    ValueType type = SYValueTypeUtil::fromString(canonicalType);

    auto &wm = WatchManager::shared();
    wm.add((uintptr_t)addr, type, "");
    if (!wm.isRunning())
        wm.start(100);

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
