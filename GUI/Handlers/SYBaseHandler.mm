#import "SYBaseHandler.h"

#import "SYInput.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "ShirayukiViewController.h"

/// Must match the identifier ShirayukiViewController registers for SYResultCell.
/// Previously each handler declared its own copy of this string.
static NSString *const kResultCellID = @"SYCell";

@interface SYBaseHandler ()
@property (nonatomic, strong) NSMutableArray *mutableEntries;
@property (nonatomic, assign) BOOL busyFlag;
@end

@implementation SYBaseHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _mutableEntries = [NSMutableArray new];
    }
    return self;
}

- (NSMutableArray *)entries {
    return _mutableEntries;
}

- (BOOL)busy {
    return _busyFlag;
}

#pragma mark - Tab metadata

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{};
}

- (NSString *)descriptorValueForKey:(NSString *)key fallback:(NSString *)fallback {
    NSString *value = [[self class] tabDescriptor][key];
    return value.length ? value : fallback;
}

- (NSString *)tabTitle {
    return [self descriptorValueForKey:@"title" fallback:@"Tab"];
}

- (NSString *)tabIcon {
    return [self descriptorValueForKey:@"icon" fallback:@"questionmark"];
}

- (NSString *)placeholder {
    return [self descriptorValueForKey:@"placeholder" fallback:@""];
}

- (NSString *)typeLabel {
    return [self descriptorValueForKey:@"typeLabel" fallback:@"—"];
}

- (NSString *)actionIcon {
    return [self descriptorValueForKey:@"actionIcon" fallback:@"play.fill"];
}

#pragma mark - Subclass hooks

- (void)performAction:(NSString *)input {
    // Subclass responsibility.
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    // Subclass responsibility.
}

#pragma mark - Table

- (NSInteger)numberOfRows {
    return (NSInteger)self.entries.count;
}

- (SYResultCell *)dequeueCellIn:(UITableView *)tableView row:(NSInteger)row {
    return [tableView dequeueReusableCellWithIdentifier:kResultCellID
                                           forIndexPath:[NSIndexPath indexPathForRow:row
                                                                           inSection:0]];
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell = [self dequeueCellIn:tableView row:row];
    if (row >= 0 && row < [self numberOfRows])
        [self configureCell:cell forRow:row];
    return cell;
}

- (BOOL)canDeleteRow:(NSInteger)row {
    return YES;
}

- (void)deleteRow:(NSInteger)row {
    if (row >= 0 && row < (NSInteger)self.entries.count)
        [self.entries removeObjectAtIndex:(NSUInteger)row];
}

#pragma mark - Helpers

- (void)runInBackground:(void (^)(void))work completion:(void (^)(void))completion {
    if (_busyFlag) {
        [SYToast show:@"Already running" type:SYToastWarning];
        return;
    }
    if (!work)
        return;

    _busyFlag = YES;

    // Weak self so a handler released while a long scan is running is not kept
    // alive by the block, and the completion is simply dropped.
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        work();
        dispatch_async(dispatch_get_main_queue(), ^{
            __strong __typeof__(weakSelf) strongSelf = weakSelf;
            if (!strongSelf)
                return;
            strongSelf.busyFlag = NO;
            if (completion)
                completion();
        });
    });
}

- (void)copyAddressToClipboard:(uintptr_t)address {
    NSString *text = SYFormatAddress(address);
    [UIPasteboard generalPasteboard].string = text;
    [SYToast show:[NSString stringWithFormat:@"Copied %@", text] type:SYToastInfo];
}

- (void)finishWithMessage:(NSString *)message type:(SYToastType)type {
    if (message.length)
        [SYToast show:message type:type];
    [self.viewController reloadTable];
}

- (void)failWithMessage:(NSString *)message {
    [SYToast show:message.length ? message : @"Failed" type:SYToastError];
}

@end
