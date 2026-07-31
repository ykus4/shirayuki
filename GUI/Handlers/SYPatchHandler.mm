#import "SYPatchHandler.h"

#import "SYInput.h"
#import "SYPatchStore.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static const CGFloat kCellIconSize = 14;

@interface SYPatchHandler ()
@property (nonatomic, strong) SYPatchStore *store;
@end

@implementation SYPatchHandler

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{
        @"title" : @"Patch",
        @"icon" : @"wrench.and.screwdriver",
        @"placeholder" : @"0xADDR HEXBYTES [label]",
        @"typeLabel" : @"hex",
        @"actionIcon" : @"hammer.fill",
    };
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _store = [[SYPatchStore alloc] init];
    }
    return self;
}

- (NSArray<NSDictionary *> *)allPatches {
    return [_store serializedEntries];
}

#pragma mark - Input

/// Split the arguments into hex bytes and an optional trailing label.
///
/// The previous version walked the tokens with a hand-rolled classifier that
/// treated any token of length <= 2 made of hex digits as bytes and everything
/// else as the label. It contained a provably dead inner `if` (guarded by
/// `!hexStr.length` yet testing `hexStr.length`), and it mistook short labels
/// like "AB" for bytes. Deciding by whether the whole remaining run parses as hex
/// removes the ambiguity.
- (void)splitArgs:(NSArray<NSString *> *)args
          intoHex:(NSString **)outHex
            label:(NSString **)outLabel {
    NSUInteger hexCount = 0;
    while (hexCount < args.count && Hex::isValid([args[hexCount] UTF8String]))
        hexCount++;

    NSArray *hexTokens = [args subarrayWithRange:NSMakeRange(0, hexCount)];
    NSArray *labelTokens = [args subarrayWithRange:NSMakeRange(hexCount, args.count - hexCount)];

    *outHex = [hexTokens componentsJoinedByString:@" "];
    *outLabel = [labelTokens componentsJoinedByString:@" "];
}

- (void)performAction:(NSString *)input {
    SYCommand *cmd = [SYCommand parse:input minArgs:1];
    if (!cmd.ok) {
        [self failWithMessage:cmd.error ?: @"Format: 0xADDR HEX [label]"];
        return;
    }

    NSString *hex = nil, *label = nil;
    [self splitArgs:cmd.args intoHex:&hex label:&label];
    if (hex.length == 0) {
        [self failWithMessage:@"Format: 0xADDR HEX [label]"];
        return;
    }

    NSString *error = nil;
    SYPatchEntry *entry = [_store applyPatchAtAddress:cmd.address hex:hex label:label error:&error];
    if (!entry) {
        [self failWithMessage:error ?: @"Patch failed"];
        return;
    }

    [self
        finishWithMessage:[NSString stringWithFormat:@"Patched %@", SYFormatAddress(entry.address)]
                     type:SYToastSuccess];
}

#pragma mark - Bulk operations

- (void)restoreAll {
    NSUInteger failed = 0;
    const NSUInteger restored = [_store restoreAllWithFailureCount:&failed];

    if (restored == 0 && failed == 0) {
        [self finishWithMessage:@"No patches to restore" type:SYToastInfo];
        return;
    }
    if (failed > 0) {
        [self finishWithMessage:[NSString stringWithFormat:@"Restored %lu, %lu failed",
                                                           (unsigned long)restored,
                                                           (unsigned long)failed]
                           type:SYToastWarning];
        return;
    }
    [self finishWithMessage:[NSString
                                stringWithFormat:@"Restored %lu patches", (unsigned long)restored]
                       type:SYToastSuccess];
}

#pragma mark - Undo / redo

- (BOOL)canUndo {
    return _store.canUndo;
}

- (BOOL)canRedo {
    return _store.canRedo;
}

- (void)undo {
    NSString *error = nil;
    NSString *description = [_store undoWithError:&error];
    if (!description) {
        // Only claim something happened when it did.
        [self finishWithMessage:error ?: @"Nothing to undo" type:SYToastWarning];
        return;
    }
    [self finishWithMessage:description type:SYToastInfo];
}

- (void)redo {
    NSString *error = nil;
    NSString *description = [_store redoWithError:&error];
    if (!description) {
        [self finishWithMessage:error ?: @"Nothing to redo" type:SYToastWarning];
        return;
    }
    [self finishWithMessage:description type:SYToastSuccess];
}

#pragma mark - Menus

- (NSArray<UIAlertAction *> *)actionButtonMenuActions {
    NSMutableArray<UIAlertAction *> *actions = [NSMutableArray array];
    __weak __typeof__(self) weakSelf = self;

    if (_store.canUndo) {
        [actions addObject:[UIAlertAction actionWithTitle:@"Undo"
                                                    style:UIAlertActionStyleDefault
                                                  handler:^(UIAlertAction *a) {
                                                      [weakSelf undo];
                                                  }]];
    }
    if (_store.canRedo) {
        [actions addObject:[UIAlertAction actionWithTitle:@"Redo"
                                                    style:UIAlertActionStyleDefault
                                                  handler:^(UIAlertAction *a) {
                                                      [weakSelf redo];
                                                  }]];
    }
    if (_store.entries.count) {
        [actions addObject:[UIAlertAction actionWithTitle:@"Restore All"
                                                    style:UIAlertActionStyleDestructive
                                                  handler:^(UIAlertAction *a) {
                                                      [weakSelf restoreAll];
                                                  }]];
    }
    return actions;
}

#pragma mark - Table

- (NSInteger)numberOfRows {
    return (NSInteger)_store.entries.count;
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    SYPatchEntry *entry = _store.entries[(NSUInteger)row];
    const BOOL applied = entry.applied;
    UIColor *stateColor = applied ? [SYTheme success] : [SYTheme textMuted];

    [cell
        configureWithIcon:[SYTheme icon:@"wrench.fill" size:kCellIconSize color:stateColor]
                    title:[entry displayName]
                   detail:[NSString stringWithFormat:@"%@ → %@", entry.originalHex, entry.patchHex]
                    badge:applied ? @"ON" : @"OFF"
               badgeColor:stateColor];
}

- (void)didSelectRow:(NSInteger)row {
    if (row < 0 || (NSUInteger)row >= _store.entries.count)
        return;

    NSString *error = nil;
    if (![_store togglePatchAtIndex:(NSUInteger)row error:&error]) {
        [self failWithMessage:error ?: @"Toggle failed"];
        return;
    }
    const BOOL applied = _store.entries[(NSUInteger)row].applied;
    [self finishWithMessage:applied ? @"Re-applied" : @"Restored"
                       type:applied ? SYToastSuccess : SYToastInfo];
}

- (void)didLongPressRow:(NSInteger)row {
    if (row < 0 || (NSUInteger)row >= _store.entries.count)
        return;
    [self copyAddressToClipboard:_store.entries[(NSUInteger)row].address];
}

- (BOOL)canDeleteRow:(NSInteger)row {
    return YES;
}

- (void)deleteRow:(NSInteger)row {
    if (row < 0 || (NSUInteger)row >= _store.entries.count)
        return;

    NSString *error = nil;
    if (![_store removePatchAtIndex:(NSUInteger)row error:&error])
        [self failWithMessage:error ?: @"Could not restore original bytes"];
}

@end
