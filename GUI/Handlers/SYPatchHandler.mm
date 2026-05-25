#import "SYPatchHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYPatchHandler ()
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *patches;
// Each undo item targets a stable patch id so row deletion/reordering cannot affect it.
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *undoStack;
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *redoStack;
@property (nonatomic, assign) NSUInteger nextPatchId;
@end

@implementation SYPatchHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _patches = [NSMutableArray new];
        _undoStack = [NSMutableArray new];
        _redoStack = [NSMutableArray new];
        _nextPatchId = 1;
    }
    return self;
}

- (NSInteger)indexForPatchId:(NSNumber *)patchId {
    for (NSUInteger i = 0; i < _patches.count; i++) {
        if ([_patches[i][@"id"] isEqualToNumber:patchId])
            return (NSInteger)i;
    }
    return -1;
}

- (NSArray<NSDictionary *> *)allPatches {
    return [_patches copy];
}

- (NSString *)tabTitle {
    return @"Patch";
}
- (NSString *)tabIcon {
    return @"wrench.and.screwdriver";
}
- (NSString *)placeholder {
    return @"0xADDR HEXBYTES [label]";
}
- (NSString *)typeLabel {
    return @"hex";
}
- (NSString *)actionIcon {
    return @"hammer.fill";
}

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    if (parts.count < 2) {
        [SYToast show:@"Format: 0xADDR HEX [label]" type:SYToastWarning];
        return;
    }

    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    if (!addr) {
        [SYToast show:@"Invalid address" type:SYToastError];
        return;
    }

    NSMutableString *hexStr = [NSMutableString new];
    NSString *label = @"";

    for (NSUInteger i = 1; i < parts.count; i++) {
        NSString *part = parts[i];
        // Check if it looks like hex
        if (part.length <= 2 &&
            [[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEFabcdef"]
                isSupersetOfSet:[NSCharacterSet characterSetWithCharactersInString:part]]) {
            if (hexStr.length)
                [hexStr appendString:@" "];
            [hexStr appendString:part];
        } else if (!hexStr.length) {
            if (hexStr.length)
                [hexStr appendString:@" "];
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
            @"id" : @(_nextPatchId++),
            @"address" : @(addr),
            @"hex" : hexStr,
            @"original" : @(patch.originalHex().c_str()),
            @"label" : label,
            @"applied" : @YES
        } mutableCopy];
        [_patches addObject:entry];
        // Record undo: "remove the last entry"
        [_undoStack addObject:@{
            @"action" : @"remove",
            @"id" : entry[@"id"],
            @"index" : @(_patches.count - 1)
        }];
        [_redoStack removeAllObjects];
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

- (NSInteger)numberOfRows {
    return _patches.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    NSDictionary *entry = _patches[row];
    BOOL applied = [entry[@"applied"] boolValue];
    NSString *lbl =
        [entry[@"label"] length]
            ? entry[@"label"]
            : [NSString stringWithFormat:@"0x%llX", [entry[@"address"] unsignedLongLongValue]];

    [cell
        configureWithIcon:[SYTheme icon:@"wrench.fill"
                                   size:14
                                  color:applied ? [SYTheme success] : [SYTheme textMuted]]
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
        [_undoStack addObject:@{@"action" : @"reapply", @"id" : entry[@"id"], @"index" : @(row)}];
        [_redoStack removeAllObjects];
        [SYToast show:@"Restored" type:SYToastInfo];
    } else {
        auto bytes = Hex::toBytes([entry[@"hex"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @YES;
        [_undoStack addObject:@{@"action" : @"restore", @"id" : entry[@"id"], @"index" : @(row)}];
        [_redoStack removeAllObjects];
        [SYToast show:@"Re-applied" type:SYToastSuccess];
    }
    [self.viewController reloadTable];
}

- (BOOL)canDeleteRow:(NSInteger)row {
    return YES;
}
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
    [UIPasteboard generalPasteboard].string =
        [NSString stringWithFormat:@"0x%lX", (unsigned long)addr];
    [SYToast show:@"Address copied" type:SYToastInfo];
}

- (BOOL)canUndo {
    return _undoStack.count > 0;
}
- (BOOL)canRedo {
    return _redoStack.count > 0;
}

- (void)undo {
    if (!_undoStack.count)
        return;
    NSDictionary *item = _undoStack.lastObject;
    [_undoStack removeLastObject];

    NSString *action = item[@"action"];
    NSNumber *patchId = item[@"id"];
    NSInteger idx = [self indexForPatchId:patchId];

    if ([action isEqualToString:@"remove"] && idx >= 0) {
        // Undo an apply: restore original bytes and remove entry
        NSMutableDictionary *entry = _patches[idx];
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"original"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        [_redoStack addObject:@{@"action" : @"readd", @"entry" : entry, @"index" : @(idx)}];
        [_patches removeObjectAtIndex:idx];
    } else if ([action isEqualToString:@"restore"] && idx >= 0) {
        // Undo a re-apply: restore original
        NSMutableDictionary *entry = _patches[idx];
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"original"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @NO;
        [_redoStack addObject:@{@"action" : @"reapply", @"id" : patchId, @"index" : @(idx)}];
    } else if ([action isEqualToString:@"reapply"] && idx >= 0) {
        // Undo a restore: re-apply patch
        NSMutableDictionary *entry = _patches[idx];
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"hex"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @YES;
        [_redoStack addObject:@{@"action" : @"restore", @"id" : patchId, @"index" : @(idx)}];
    }

    [SYToast show:@"Undone" type:SYToastInfo];
    [self.viewController reloadTable];
}

- (void)redo {
    if (!_redoStack.count)
        return;
    NSDictionary *item = _redoStack.lastObject;
    [_redoStack removeLastObject];

    NSString *action = item[@"action"];

    if ([action isEqualToString:@"readd"]) {
        // Redo an apply: re-apply and re-insert
        NSMutableDictionary *entry = [item[@"entry"] mutableCopy];
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"hex"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @YES;
        NSUInteger idx = MIN([item[@"index"] unsignedIntegerValue], _patches.count);
        [_patches insertObject:entry atIndex:idx];
        [_undoStack addObject:@{@"action" : @"remove", @"id" : entry[@"id"], @"index" : @(idx)}];
    } else if ([action isEqualToString:@"reapply"]) {
        NSInteger idx = [self indexForPatchId:item[@"id"]];
        if (idx < 0)
            return;
        NSMutableDictionary *entry = _patches[idx];
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"hex"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @YES;
        [_undoStack addObject:@{@"action" : @"restore", @"id" : item[@"id"], @"index" : @(idx)}];
    } else if ([action isEqualToString:@"restore"]) {
        NSInteger idx = [self indexForPatchId:item[@"id"]];
        if (idx < 0)
            return;
        NSMutableDictionary *entry = _patches[idx];
        uintptr_t addr = [entry[@"address"] unsignedLongLongValue];
        auto bytes = Hex::toBytes([entry[@"original"] UTF8String]);
        Memory::write(addr, bytes.data(), bytes.size());
        entry[@"applied"] = @NO;
        [_undoStack addObject:@{@"action" : @"reapply", @"id" : item[@"id"], @"index" : @(idx)}];
    }

    [SYToast show:@"Redone" type:SYToastSuccess];
    [self.viewController reloadTable];
}

@end
