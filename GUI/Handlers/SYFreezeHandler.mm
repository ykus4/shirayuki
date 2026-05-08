#import "SYFreezeHandler.h"
#import "ShirayukiViewController.h"
#import "SYTheme.h"
#import "SYResultCell.h"
#import "SYToast.h"
#import "Freeze.hpp"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYFreezeHandler ()
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *entries;
@end

@implementation SYFreezeHandler

- (instancetype)init {
    self = [super init];
    if (self) { _entries = [NSMutableArray new]; }
    return self;
}

- (NSString *)tabTitle { return @"Freeze"; }
- (NSString *)tabIcon { return @"lock.fill"; }
- (NSString *)placeholder { return @"0xADDR VALUE [type:i32|f32]"; }
- (NSString *)typeLabel { return @"frz"; }
- (NSString *)actionIcon { return @"lock.fill"; }

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    if (parts.count < 2) {
        [SYToast show:@"Format: 0xADDR VALUE" type:SYToastWarning];
        return;
    }

    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    NSString *valStr = parts[1];
    NSString *typeStr = parts.count > 2 ? parts[2] : @"i32";

    auto &fm = FreezeManager::shared();

    uint64_t fid = 0;
    if ([typeStr isEqualToString:@"f32"]) {
        float v = [valStr floatValue];
        fid = fm.add(addr, &v, sizeof(float), ValueType::Float32, "");
    } else if ([typeStr isEqualToString:@"f64"]) {
        double v = [valStr doubleValue];
        fid = fm.add(addr, &v, sizeof(double), ValueType::Float64, "");
    } else if ([typeStr isEqualToString:@"i64"]) {
        int64_t v = [valStr longLongValue];
        fid = fm.add(addr, &v, sizeof(int64_t), ValueType::Int64, "");
    } else {
        int32_t v = [valStr intValue];
        fid = fm.add(addr, &v, sizeof(int32_t), ValueType::Int32, "");
    }

    if (!fm.isRunning()) fm.start(16);

    NSMutableDictionary *entry = [@{
        @"id": @(fid),
        @"address": @(addr),
        @"value": valStr,
        @"type": typeStr,
        @"active": @YES
    } mutableCopy];
    [_entries addObject:entry];

    [SYToast show:[NSString stringWithFormat:@"Frozen 0x%llX = %@", addr, valStr] type:SYToastSuccess];
    [self.viewController reloadTable];
}

- (void)removeAll {
    FreezeManager::shared().removeAll();
    [_entries removeAllObjects];
    [SYToast show:@"All freezes removed" type:SYToastInfo];
    [self.viewController reloadTable];
}

- (NSInteger)numberOfRows { return _entries.count; }

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell = [tableView dequeueReusableCellWithIdentifier:kCellID forIndexPath:
        [NSIndexPath indexPathForRow:row inSection:0]];

    NSDictionary *entry = _entries[row];
    BOOL active = [entry[@"active"] boolValue];

    [cell configureWithIcon:[SYTheme icon:active ? @"lock.fill" : @"lock.open" size:14
                                    color:active ? [SYTheme accent] : [SYTheme textMuted]]
                      title:[NSString stringWithFormat:@"0x%llX", [entry[@"address"] unsignedLongLongValue]]
                     detail:[NSString stringWithFormat:@"= %@ (%@)", entry[@"value"], entry[@"type"]]
                      badge:active ? @"FROZEN" : @"PAUSED"
                 badgeColor:active ? [SYTheme accent] : [SYTheme textMuted]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSMutableDictionary *entry = _entries[row];
    BOOL active = ![entry[@"active"] boolValue];
    entry[@"active"] = @(active);

    uint64_t fid = [entry[@"id"] unsignedLongLongValue];
    FreezeManager::shared().setActive(fid, active);

    UIImpactFeedbackGenerator *haptic = [[UIImpactFeedbackGenerator alloc]
        initWithStyle:UIImpactFeedbackStyleLight];
    [haptic impactOccurred];

    [self.viewController reloadTable];
}

- (BOOL)canDeleteRow:(NSInteger)row { return YES; }
- (void)deleteRow:(NSInteger)row {
    uint64_t fid = [_entries[row][@"id"] unsignedLongLongValue];
    FreezeManager::shared().remove(fid);
    [_entries removeObjectAtIndex:row];
}

- (void)didLongPressRow:(NSInteger)row {
    uintptr_t addr = [_entries[row][@"address"] unsignedLongLongValue];
    [UIPasteboard generalPasteboard].string = [NSString stringWithFormat:@"0x%lX", (unsigned long)addr];
    [SYToast show:@"Address copied" type:SYToastInfo];
}

@end
