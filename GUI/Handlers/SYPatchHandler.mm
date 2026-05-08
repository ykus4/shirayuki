#import "SYPatchHandler.h"
#import "ShirayukiViewController.h"
#import "SYTheme.h"
#import "SYResultCell.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYPatchHandler ()
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *patches;
@end

@implementation SYPatchHandler

- (instancetype)init {
    self = [super init];
    if (self) { _patches = [NSMutableArray new]; }
    return self;
}

- (NSString *)tabTitle { return @"Patch"; }
- (NSString *)tabIcon { return @"wrench.and.screwdriver"; }
- (NSString *)placeholder { return @"0xADDR HEXBYTES [label]"; }
- (NSString *)typeLabel { return @"hex"; }
- (NSString *)actionIcon { return @"hammer.fill"; }

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    if (parts.count < 2) {
        [SYToast show:@"Format: 0xADDR HEX [label]" type:SYToastWarning];
        return;
    }

    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    NSMutableString *hexStr = [NSMutableString new];
    NSString *label = @"";

    for (NSUInteger i = 1; i < parts.count; i++) {
        NSString *part = parts[i];
        // Check if it looks like hex
        if (part.length <= 2 && [[NSCharacterSet characterSetWithCharactersInString:
            @"0123456789ABCDEFabcdef"] isSupersetOfSet:
            [NSCharacterSet characterSetWithCharactersInString:part]]) {
            if (hexStr.length) [hexStr appendString:@" "];
            [hexStr appendString:part];
        } else if (!hexStr.length) {
            if (hexStr.length) [hexStr appendString:@" "];
            [hexStr appendString:part];
        } else {
            // Rest is label
            NSArray *remaining = [parts subarrayWithRange:NSMakeRange(i, parts.count - i)];
            label = [remaining componentsJoinedByString:@" "];
            break;
        }
    }

    auto patch = Patch::createWithHex((uintptr_t)addr, [hexStr UTF8String]);
    if (patch.isValid() && patch.apply()) {
        NSMutableDictionary *entry = [@{
            @"address": @(addr),
            @"hex": hexStr,
            @"original": @(patch.originalHex().c_str()),
            @"label": label,
            @"applied": @YES
        } mutableCopy];
        [_patches addObject:entry];
        [SYToast show:[NSString stringWithFormat:@"Patched 0x%llX", addr] type:SYToastSuccess];
    } else {
        [SYToast show:@"Patch failed" type:SYToastError];
    }
    [self.viewController reloadTable];
}

- (void)restoreAll {
    for (NSMutableDictionary *entry in _patches) {
        if ([entry[@"applied"] boolValue]) {
            uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
            auto bytes = Hex::toBytes([entry[@"original"] UTF8String]);
            if (!bytes.empty()) {
                Memory::write(addr, bytes.data(), bytes.size());
            }
            entry[@"applied"] = @NO;
        }
    }
    [SYToast show:@"All patches restored" type:SYToastSuccess];
    [self.viewController reloadTable];
}

- (NSInteger)numberOfRows { return _patches.count; }

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell = [tableView dequeueReusableCellWithIdentifier:kCellID forIndexPath:
        [NSIndexPath indexPathForRow:row inSection:0]];

    NSDictionary *entry = _patches[row];
    BOOL applied = [entry[@"applied"] boolValue];
    NSString *lbl = [entry[@"label"] length] ? entry[@"label"] :
        [NSString stringWithFormat:@"0x%llX", [entry[@"address"] unsignedLongLongValue]];

    [cell configureWithIcon:[SYTheme icon:@"wrench.fill" size:14 color:applied ? [SYTheme success] : [SYTheme textMuted]]
                      title:lbl
                     detail:[NSString stringWithFormat:@"%@ → %@", entry[@"original"], entry[@"hex"]]
                      badge:applied ? @"ON" : @"OFF"
                 badgeColor:applied ? [SYTheme success] : [SYTheme textMuted]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSMutableDictionary *entry = _patches[row];
    BOOL applied = [entry[@"applied"] boolValue];
    uintptr_t addr = [entry[@"address"] unsignedLongLongValue];

    if (applied) {
        auto bytes = Hex::toBytes([entry[@"original"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @NO;
        [SYToast show:@"Restored" type:SYToastInfo];
    } else {
        auto bytes = Hex::toBytes([entry[@"hex"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @YES;
        [SYToast show:@"Re-applied" type:SYToastSuccess];
    }
    [self.viewController reloadTable];
}

- (BOOL)canDeleteRow:(NSInteger)row { return YES; }
- (void)deleteRow:(NSInteger)row {
    // Restore before removing
    NSMutableDictionary *entry = _patches[row];
    if ([entry[@"applied"] boolValue]) {
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"original"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
    }
    [_patches removeObjectAtIndex:row];
}

- (void)didLongPressRow:(NSInteger)row {
    uintptr_t addr = [_patches[row][@"address"] unsignedLongLongValue];
    [UIPasteboard generalPasteboard].string = [NSString stringWithFormat:@"0x%lX", (unsigned long)addr];
    [SYToast show:@"Address copied" type:SYToastInfo];
}

@end
