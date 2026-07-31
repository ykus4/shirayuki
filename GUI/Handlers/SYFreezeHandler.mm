#import "SYFreezeHandler.h"

#import "Freeze.hpp"
#import "SYInput.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static const CGFloat kCellIconSize = 14;

/// Amount added to the frozen value on every freeze tick when auto-increment is
/// on. There is no UI to choose a step, and the tick rate is `kFreezeIntervalMs`,
/// so anything larger than the smallest unit runs away far too fast to be useful.
static const int64_t kAutoIncrementStep = 1;

/// Type used when the command line omits one.
static NSString *const kDefaultTypeTag = @"int32";

/// Row keys. Named so a typo is a compile error rather than a silently nil read.
static NSString *const kKeyID = @"id";
static NSString *const kKeyAddress = @"address";
static NSString *const kKeyValue = @"value";
static NSString *const kKeyType = @"type";
static NSString *const kKeyActive = @"active";
static NSString *const kKeyAutoIncrement = @"autoIncrement";

/// What a row is doing, derived once from the (autoIncrement, active) pair.
///
/// The cell used to derive its badge text, badge colour, icon name and icon
/// colour from that pair with four separate nested ternaries — four places to
/// keep in step for three states, two of which computed the identical colour.
typedef NS_ENUM(NSInteger, SYFreezeRowState) {
    SYFreezeRowStatePaused,
    SYFreezeRowStateFrozen,
    SYFreezeRowStateIncrementing,
};

@implementation SYFreezeHandler

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{
        @"title" : @"Freeze",
        @"icon" : @"lock.fill",
        @"placeholder" : @"0xADDR VALUE [type:i32|f32]",
        @"typeLabel" : @"frz",
        @"actionIcon" : @"lock.fill"
    };
}

#pragma mark - Actions

- (void)performAction:(NSString *)input {
    SYCommand *command = [SYCommand parse:input minArgs:1];
    if (!command.ok) {
        [self failWithMessage:command.error];
        return;
    }

    NSString *valueText = [command argAt:0];
    NSString *typeTag = [command argAt:1 or:kDefaultTypeTag];

    // `tryFromString` takes both short ("f32") and canonical ("float") tags, so
    // the hand-written translation table is gone. Its two copies had already
    // diverged — the watch tab's knew "i8" and the freeze tab's did not, and they
    // fell back differently for anything unknown. Reporting the bad tag beats
    // freezing four bytes as an int32 the user never asked for.
    ValueType type = ValueType::Int32;
    if (!SYValueTypeUtil::tryFromString(typeTag, type)) {
        [self failWithMessage:[NSString stringWithFormat:@"Unknown type '%@'", typeTag]];
        return;
    }
    NSString *canonicalTag = SYValueTypeUtil::canonicalTag(type);

    uint8_t value[kMaxValueSize] = {};
    const size_t width = SYValueTypeUtil::parseValue(valueText, canonicalTag, value);
    if (width == 0) {
        [self failWithMessage:[NSString stringWithFormat:@"'%@' is not a valid %@", valueText,
                                                         canonicalTag]];
        return;
    }

    auto &manager = FreezeManager::shared();
    const uint64_t identifier = manager.add(command.address, value, width, type, "");
    if (identifier == 0) {
        // Entry IDs start at 1, so zero means the manager refused. Adding the row
        // regardless left a phantom "FROZEN" line for an address nothing was
        // holding, and a delete on it would have removed some other entry's ID.
        [self failWithMessage:@"Could not create freeze entry"];
        return;
    }

    if (!manager.isRunning())
        manager.start(kFreezeIntervalMs);

    // The stored value is round-tripped through the parser rather than kept as
    // typed, so the row shows the value actually being written. The stored type
    // is the canonical tag, not the user's spelling: the cell used to echo
    // whatever was typed, including tags that had fallen back to int32 and so
    // labelled a row with a type it was not frozen as.
    NSMutableDictionary *entry = [NSMutableDictionary dictionary];
    entry[kKeyID] = @(identifier);
    entry[kKeyAddress] = @((unsigned long long)command.address);
    entry[kKeyValue] = SYValueTypeUtil::formatValue(value, canonicalTag);
    entry[kKeyType] = canonicalTag;
    entry[kKeyActive] = @YES;
    entry[kKeyAutoIncrement] = @NO;
    [self.entries addObject:entry];

    [self finishWithMessage:[NSString stringWithFormat:@"Frozen %@ = %@",
                                                       SYFormatAddress(command.address), valueText]
                       type:SYToastSuccess];
}

- (void)removeAll {
    FreezeManager::shared().removeAll();
    [self.entries removeAllObjects];
    [self finishWithMessage:@"All freezes removed" type:SYToastInfo];
}

- (void)toggleAutoIncrementForRow:(NSInteger)row {
    NSMutableDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return;

    const BOOL enabled = ![entry[kKeyAutoIncrement] boolValue];
    entry[kKeyAutoIncrement] = @(enabled);
    FreezeManager::shared().setAutoIncrement([entry[kKeyID] unsignedLongLongValue], enabled,
                                             kAutoIncrementStep);

    [self finishWithMessage:enabled ? @"Auto-increment ON" : @"Auto-increment OFF"
                       type:enabled ? SYToastSuccess : SYToastInfo];
}

#pragma mark - Menus

/// Per-row long-press menu. These actions used to be built inside
/// ShirayukiViewController behind a `_currentTabIndex == 2` check, which meant
/// the view controller had to know that row menus were a freeze-tab feature.
- (NSArray<UIAlertAction *> *)contextActionsForRow:(NSInteger)row {
    NSMutableDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return @[];

    const BOOL incrementing = [entry[kKeyAutoIncrement] boolValue];
    const BOOL active = [entry[kKeyActive] boolValue];
    __weak __typeof__(self) weakSelf = self;

    return @[
        [UIAlertAction
            actionWithTitle:incrementing ? @"Stop Auto-Increment" : @"Start Auto-Increment"
                      style:UIAlertActionStyleDefault
                    handler:^(UIAlertAction *a) {
                        [weakSelf toggleAutoIncrementForRow:row];
                    }],
        [UIAlertAction actionWithTitle:active ? @"Pause" : @"Resume"
                                 style:UIAlertActionStyleDefault
                               handler:^(UIAlertAction *a) {
                                   [weakSelf toggleActiveForRow:row];
                               }],
        [UIAlertAction actionWithTitle:@"Copy Address"
                                 style:UIAlertActionStyleDefault
                               handler:^(UIAlertAction *a) {
                                   [weakSelf didLongPressRow:row];
                               }],
    ];
}

/// Action-button long press: bulk operations. `removeAll` previously had no
/// caller at all, so there was no way to clear the list except row by row.
- (NSArray<UIAlertAction *> *)actionButtonMenuActions {
    if (self.entries.count == 0)
        return @[];

    __weak __typeof__(self) weakSelf = self;
    return @[
        [UIAlertAction actionWithTitle:@"Remove All Freezes"
                                 style:UIAlertActionStyleDestructive
                               handler:^(UIAlertAction *a) {
                                   [weakSelf removeAll];
                               }],
    ];
}

/// Pause or resume a single entry. FreezeManager already supported this; nothing
/// in the UI reached it.
- (void)toggleActiveForRow:(NSInteger)row {
    NSMutableDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return;

    const BOOL active = ![entry[kKeyActive] boolValue];
    entry[kKeyActive] = @(active);
    FreezeManager::shared().setActive([entry[kKeyID] unsignedLongLongValue], active);

    [self finishWithMessage:active ? @"Resumed" : @"Paused"
                       type:active ? SYToastSuccess : SYToastInfo];
}

#pragma mark - Rows

/// Bounds-checked row lookup. Every entry point below is driven by a tap or a
/// gesture whose index can outlive the row it was captured for.
- (NSMutableDictionary *)entryAtRow:(NSInteger)row {
    if (row < 0 || (NSUInteger)row >= self.entries.count)
        return nil;
    return self.entries[(NSUInteger)row];
}

/// Precedence matches the ternaries this replaced: auto-increment outranks the
/// active flag, so a paused auto-increment row still reads INC.
- (SYFreezeRowState)stateForEntry:(NSDictionary *)entry {
    if ([entry[kKeyAutoIncrement] boolValue])
        return SYFreezeRowStateIncrementing;
    return [entry[kKeyActive] boolValue] ? SYFreezeRowStateFrozen : SYFreezeRowStatePaused;
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    NSDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return;

    NSString *badge;
    NSString *icon;
    UIColor *tint;
    switch ([self stateForEntry:entry]) {
        case SYFreezeRowStateIncrementing:
            badge = @"INC";
            icon = @"arrow.up.circle.fill";
            tint = [SYTheme warning];
            break;
        case SYFreezeRowStateFrozen:
            badge = @"FROZEN";
            icon = @"lock.fill";
            tint = [SYTheme accent];
            break;
        case SYFreezeRowStatePaused:
            badge = @"PAUSED";
            icon = @"lock.open";
            tint = [SYTheme textMuted];
            break;
    }

    const uintptr_t address = (uintptr_t)[entry[kKeyAddress] unsignedLongLongValue];
    [cell configureWithIcon:[SYTheme icon:icon size:kCellIconSize color:tint]
                      title:SYFormatAddress(address)
                     detail:[NSString
                                stringWithFormat:@"= %@ (%@)", entry[kKeyValue], entry[kKeyType]]
                      badge:badge
                 badgeColor:tint];
}

- (void)didSelectRow:(NSInteger)row {
    NSMutableDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return;

    const BOOL active = ![entry[kKeyActive] boolValue];
    entry[kKeyActive] = @(active);
    FreezeManager::shared().setActive([entry[kKeyID] unsignedLongLongValue], active);
    [self.viewController reloadTable];
}

- (void)didLongPressRow:(NSInteger)row {
    NSMutableDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return;
    [self copyAddressToClipboard:(uintptr_t)[entry[kKeyAddress] unsignedLongLongValue]];
}

- (void)deleteRow:(NSInteger)row {
    NSMutableDictionary *entry = [self entryAtRow:row];
    if (!entry)
        return;
    FreezeManager::shared().remove([entry[kKeyID] unsignedLongLongValue]);
    [super deleteRow:row];
}

@end
