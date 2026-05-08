#import "SYFreezeHandler.h"
#import "Freeze.hpp"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYFreezeHandler ()
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *entries;
@end

@implementation SYFreezeHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _entries = [NSMutableArray new];
    }
    return self;
}

- (NSString *)tabTitle {
    return @"Freeze";
}
- (NSString *)tabIcon {
    return @"lock.fill";
}
- (NSString *)placeholder {
    return @"0xADDR VALUE [type:i32|f32]";
}
- (NSString *)typeLabel {
    return @"frz";
}
- (NSString *)actionIcon {
    return @"lock.fill";
}

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    if (parts.count < 2) {
        [SYToast show:@"Format: 0xADDR VALUE" type:SYToastWarning];
        return;
    }

    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    NSString *valStr = parts[1];
    NSString *typeStr = parts.count > 2 ? parts[2] : @"i32";

    // Map short type tags (f32/f64/i64/i32) to canonical names for util
    NSDictionary *typeMap = @{
        @"f32" : @"float",
        @"f64" : @"double",
        @"i64" : @"int64",
        @"i32" : @"int32",
        @"i16" : @"int16"
    };
    NSString *canonicalType = typeMap[typeStr] ?: typeStr;
    ValueType vtype = SYValueTypeUtil::fromString(canonicalType);
    uint8_t buf[8] = {};
    size_t valSize = SYValueTypeUtil::parseValue(valStr, canonicalType, buf);

    auto &fm = FreezeManager::shared();
    uint64_t fid = fm.add(addr, buf, valSize, vtype, "");

    if (!fm.isRunning())
        fm.start(16);

    NSMutableDictionary *entry = [@{
        @"id" : @(fid),
        @"address" : @(addr),
        @"value" : valStr,
        @"type" : typeStr,
        @"active" : @YES
    } mutableCopy];
    [_entries addObject:entry];

    [SYToast show:[NSString stringWithFormat:@"Frozen 0x%llX = %@", addr, valStr]
             type:SYToastSuccess];
    [self.viewController reloadTable];
}

- (void)removeAll {
    FreezeManager::shared().removeAll();
    [_entries removeAllObjects];
    [SYToast show:@"All freezes removed" type:SYToastInfo];
    [self.viewController reloadTable];
}

- (NSInteger)numberOfRows {
    return _entries.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    NSDictionary *entry = _entries[row];
    BOOL active = [entry[@"active"] boolValue];
    BOOL autoInc = [entry[@"autoIncrement"] boolValue];

    NSString *badgeText = autoInc ? @"INC" : (active ? @"FROZEN" : @"PAUSED");
    UIColor *badgeColor =
        autoInc ? [SYTheme warning] : (active ? [SYTheme accent] : [SYTheme textMuted]);
    NSString *icon = autoInc ? @"arrow.up.circle.fill" : (active ? @"lock.fill" : @"lock.open");
    UIColor *iconColor =
        autoInc ? [SYTheme warning] : (active ? [SYTheme accent] : [SYTheme textMuted]);

    [cell
        configureWithIcon:[SYTheme icon:icon size:14 color:iconColor]
                    title:[NSString
                              stringWithFormat:@"0x%llX", [entry[@"address"] unsignedLongLongValue]]
                   detail:[NSString stringWithFormat:@"= %@ (%@)", entry[@"value"], entry[@"type"]]
                    badge:badgeText
               badgeColor:badgeColor];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSMutableDictionary *entry = _entries[row];
    BOOL active = ![entry[@"active"] boolValue];
    entry[@"active"] = @(active);

    uint64_t fid = [entry[@"id"] unsignedLongLongValue];
    FreezeManager::shared().setActive(fid, active);

    UIImpactFeedbackGenerator *haptic =
        [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    [haptic impactOccurred];

    [self.viewController reloadTable];
}

- (BOOL)canDeleteRow:(NSInteger)row {
    return YES;
}
- (void)deleteRow:(NSInteger)row {
    uint64_t fid = [_entries[row][@"id"] unsignedLongLongValue];
    FreezeManager::shared().remove(fid);
    [_entries removeObjectAtIndex:row];
}

- (void)didLongPressRow:(NSInteger)row {
    uintptr_t addr = [_entries[row][@"address"] unsignedLongLongValue];
    [UIPasteboard generalPasteboard].string =
        [NSString stringWithFormat:@"0x%lX", (unsigned long)addr];
    [SYToast show:@"Address copied" type:SYToastInfo];
}

- (void)toggleAutoIncrementForRow:(NSInteger)row {
    if (row >= (NSInteger)_entries.count)
        return;
    NSMutableDictionary *entry = _entries[row];
    uint64_t fid = [entry[@"id"] unsignedLongLongValue];
    BOOL current = [entry[@"autoIncrement"] boolValue];
    BOOL next = !current;
    entry[@"autoIncrement"] = @(next);
    FreezeManager::shared().setAutoIncrement(fid, next, 1);
    NSString *msg = next ? @"Auto-increment ON" : @"Auto-increment OFF";
    [SYToast show:msg type:next ? SYToastSuccess : SYToastInfo];
    [self.viewController reloadTable];
}

@end
