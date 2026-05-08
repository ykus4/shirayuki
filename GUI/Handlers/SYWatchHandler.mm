#import "SYWatchHandler.h"
#import "ShirayukiViewController.h"
#import "SYTheme.h"
#import "SYResultCell.h"
#import "SYToast.h"
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
        _refreshTimer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) {
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

- (NSString *)tabTitle { return @"Watch"; }
- (NSString *)tabIcon { return @"eye"; }
- (NSString *)placeholder { return @"0xADDR [type:i32|f32|i64]"; }
- (NSString *)typeLabel { return @"eye"; }
- (NSString *)actionIcon { return @"plus.circle.fill"; }

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    if (!addr) {
        [SYToast show:@"Invalid address" type:SYToastError];
        return;
    }

    ValueType type = ValueType::Int32;
    if (parts.count > 1) {
        NSString *t = parts[1];
        if ([t isEqualToString:@"f32"]) type = ValueType::Float32;
        else if ([t isEqualToString:@"f64"]) type = ValueType::Float64;
        else if ([t isEqualToString:@"i64"]) type = ValueType::Int64;
        else if ([t isEqualToString:@"i16"]) type = ValueType::Int16;
        else if ([t isEqualToString:@"i8"]) type = ValueType::Int8;
    }

    auto &wm = WatchManager::shared();
    wm.add((uintptr_t)addr, type, "");
    if (!wm.isRunning()) wm.start(100);

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
    SYResultCell *cell = [tableView dequeueReusableCellWithIdentifier:kCellID forIndexPath:
        [NSIndexPath indexPathForRow:row inSection:0]];

    auto entries = WatchManager::shared().entries();
    if (row >= (NSInteger)entries.size()) return cell;

    auto &entry = entries[row];
    NSString *valueStr = @(WatchManager::formatValue(entry).c_str());
    NSString *addrStr = [NSString stringWithFormat:@"0x%lX", entry.address];

    UIColor *iconColor = entry.hasChanged ? [SYTheme warning] : [SYTheme success];
    NSString *badge = [NSString stringWithFormat:@"%llu", entry.changeCount];

    [cell configureWithIcon:[SYTheme icon:entry.hasChanged ? @"bolt.fill" : @"eye.fill" size:14 color:iconColor]
                      title:addrStr
                     detail:[NSString stringWithFormat:@"= %@ [%s]", valueStr, valueTypeLabel(entry.type).c_str()]
                      badge:entry.changeCount > 0 ? badge : nil
                 badgeColor:[SYTheme accentDim]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    auto entries = WatchManager::shared().entries();
    if (row >= (NSInteger)entries.size()) return;

    uintptr_t addr = entries[row].address;
    [UIPasteboard generalPasteboard].string = [NSString stringWithFormat:@"0x%lX", addr];
    [SYToast show:@"Address copied" type:SYToastInfo];
}

- (BOOL)canDeleteRow:(NSInteger)row { return YES; }
- (void)deleteRow:(NSInteger)row {
    auto entries = WatchManager::shared().entries();
    if (row < (NSInteger)entries.size()) {
        WatchManager::shared().remove(entries[row].id);
    }
}

@end
