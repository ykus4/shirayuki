#import "SYSearchHandler.h"

#import "SYInput.h"
#import "SYResultCell.h"
#import "SYScanHelper.hpp"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

/// Non-numeric search modes, appended to the ValueType tags in the type cycle.
static NSString *const kModeHex = @"hex";
static NSString *const kModeString = @"string";
static NSString *const kModeRegex = @"regex";

static const NSUInteger kMaxHistoryEntries = 20;
static const CGFloat kCellIconSize = 14;

@interface SYSearchHandler ()
@property (nonatomic, strong) NSMutableArray<NSString *> *history;
@end

@implementation SYSearchHandler {
    /// Current result set, only ever touched on the main thread. Replaced
    /// wholesale when a scan finishes rather than mutated in place, so the table
    /// can never read a half-updated set — the previous code handed the same
    /// NSMutableDictionary objects to both threads and mutated them from the
    /// background one.
    SYScan::Result _result;
}

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{@"title" : @"Search", @"icon" : @"magnifyingglass"};
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _history = [NSMutableArray new];
        _searchType = @"int32";
    }
    return self;
}

#pragma mark - State-dependent tab metadata

- (NSString *)placeholder {
    return self.isNarrowing ? @"New value, or empty to filter changed…"
                            : @"Value, pattern, or string…";
}

- (NSString *)typeLabel {
    return [self shortType];
}

- (NSString *)actionIcon {
    return self.isNarrowing ? @"line.3.horizontal.decrease" : @"play.fill";
}

- (BOOL)hasResults {
    return _result.count() > 0;
}

- (BOOL)isNarrowing {
    // Only typed scans carry snapshots, so only they can be narrowed. Hex,
    // string and regex results have nothing to compare against.
    return _result.count() > 0 && _result.narrowable();
}

#pragma mark - Type selection

- (BOOL)isNumericType {
    return !([_searchType isEqualToString:kModeHex] || [_searchType isEqualToString:kModeString] ||
             [_searchType isEqualToString:kModeRegex]);
}

- (NSString *)shortType {
    if ([_searchType isEqualToString:kModeHex])
        return @"hex";
    if ([_searchType isEqualToString:kModeString])
        return @"str";
    if ([_searchType isEqualToString:kModeRegex])
        return @"rex";
    return SYValueTypeUtil::shortLabel(SYValueTypeUtil::fromString(_searchType));
}

/// Every ValueType plus the three non-numeric modes. Derived from the C++
/// descriptor table, so adding a type needs no change here — the previous
/// hardcoded list silently omitted int8 and all the unsigned types.
- (NSArray<NSString *> *)allSearchTypes {
    static NSArray<NSString *> *types = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSMutableArray *all = [SYValueTypeUtil::allTags() mutableCopy];
        [all addObjectsFromArray:@[ kModeHex, kModeString, kModeRegex ]];
        types = [all copy];
    });
    return types;
}

- (void)cycleType {
    NSArray<NSString *> *types = [self allSearchTypes];
    NSUInteger idx = [types indexOfObject:_searchType];
    if (idx == NSNotFound)
        idx = 0;
    self.searchType = types[(idx + 1) % types.count];

    // Snapshots belong to the previous type; keeping them would compare
    // unrelated bytes on the next narrow.
    [self clearResults];
}

#pragma mark - Actions

- (void)performAction:(NSString *)input {
    if (self.isNarrowing) {
        if (input.length == 0) {
            [self narrowWithFilter:SYNarrowChanged input:nil];
        } else {
            [self narrowWithFilter:SYNarrowExact input:input];
        }
        return;
    }
    [self performSearch:input];
}

- (void)performSearch:(NSString *)input {
    if (input.length == 0) {
        [self failWithMessage:@"Enter a value to search for"];
        return;
    }

    [self recordHistory:input];

    SYScan::Request request;
    request.typeTag = [_searchType UTF8String];
    request.input = [input UTF8String];
    request.maxResults = kMaxScanResults;
    request.maxRegionSize = kMaxRegionSize;

    // Heap-allocated so the result survives the hop back to the main queue
    // without copying a potentially large vector.
    auto *scanned = new SYScan::Result();

    __weak __typeof__(self) weakSelf = self;
    [self
        runInBackground:^{
            *scanned = SYScan::scanAll(request);
        }
        completion:^{
            __strong __typeof__(weakSelf) strongSelf = weakSelf;
            if (strongSelf)
                [strongSelf adoptResult:*scanned verb:@"results"];
            delete scanned;
        }];
}

- (void)narrowWithFilter:(SYNarrowFilter)filter input:(NSString *)input {
    if (!self.isNarrowing) {
        [self failWithMessage:@"Run a numeric search first"];
        return;
    }

    CompareMode mode = CompareMode::Changed;
    BOOL needsInput = NO;
    switch (filter) {
        case SYNarrowChanged:
            mode = CompareMode::Changed;
            break;
        case SYNarrowUnchanged:
            mode = CompareMode::Unchanged;
            break;
        case SYNarrowIncreased:
            mode = CompareMode::Increased;
            break;
        case SYNarrowDecreased:
            mode = CompareMode::Decreased;
            break;
        case SYNarrowGreaterThan:
            mode = CompareMode::GreaterThan;
            needsInput = YES;
            break;
        case SYNarrowLessThan:
            mode = CompareMode::LessThan;
            needsInput = YES;
            break;
        case SYNarrowExact:
            mode = CompareMode::Exact;
            needsInput = YES;
            break;
    }

    if (needsInput && input.length == 0) {
        [self failWithMessage:@"Enter a value to compare against"];
        return;
    }

    SYScan::NarrowRequest request;
    request.addresses = _result.addresses;
    request.snapshots = _result.snapshots;
    request.valueSize = _result.valueSize;
    request.typeTag = [_searchType UTF8String];
    request.mode = mode;
    if (needsInput)
        request.compareInput = [input UTF8String];

    auto *narrowed = new SYScan::Result();

    __weak __typeof__(self) weakSelf = self;
    [self
        runInBackground:^{
            *narrowed = SYScan::narrow(request);
        }
        completion:^{
            __strong __typeof__(weakSelf) strongSelf = weakSelf;
            if (strongSelf)
                [strongSelf adoptResult:*narrowed verb:@"remaining"];
            delete narrowed;
        }];
}

/// Install a finished scan/narrow result and report it.
- (void)adoptResult:(const SYScan::Result &)incoming verb:(NSString *)verb {
    if (!incoming.ok()) {
        [self failWithMessage:@(SYScan::describe(incoming.error).c_str())];
        return;
    }

    _result = incoming;

    const size_t count = _result.count();
    NSString *message = [NSString stringWithFormat:@"%zu %@", count, verb];
    // A scan capped at the result limit is reported, rather than silently
    // presenting a truncated set as if it were complete.
    if (count >= kMaxScanResults)
        message =
            [NSString stringWithFormat:@"%zu %@ (limit reached — narrow further)", count, verb];

    [self finishWithMessage:message type:count > 0 ? SYToastSuccess : SYToastWarning];
}

- (void)batchModify:(NSString *)value {
    if (_result.count() == 0) {
        [self failWithMessage:@"No results to modify"];
        return;
    }
    if (![self isNumericType]) {
        [self failWithMessage:@"Batch modify needs a numeric search type"];
        return;
    }

    SYScan::Error error = SYScan::Error::None;
    const size_t written =
        SYScan::writeAll(_result.addresses, [_searchType UTF8String], [value UTF8String], error);

    if (error != SYScan::Error::None) {
        [self failWithMessage:@(SYScan::describe(error).c_str())];
        return;
    }

    const size_t total = _result.count();
    if (written == 0) {
        [self failWithMessage:@"No addresses could be written"];
        return;
    }
    NSString *message = (written == total)
                            ? [NSString stringWithFormat:@"Modified %zu addresses", written]
                            : [NSString stringWithFormat:@"Modified %zu of %zu (%zu failed)",
                                                         written, total, total - written];
    [self finishWithMessage:message type:written == total ? SYToastSuccess : SYToastWarning];
}

- (void)clearResults {
    _result = SYScan::Result();
}

- (void)resetSearch {
    [self clearResults];
    [self.viewController reloadTable];
}

#pragma mark - History

- (void)recordHistory:(NSString *)input {
    if (input.length == 0)
        return;
    [_history removeObject:input];
    [_history insertObject:input atIndex:0];
    while (_history.count > kMaxHistoryEntries)
        [_history removeLastObject];
}

- (NSArray<NSString *> *)searchHistory {
    return [_history copy];
}

#pragma mark - Export

- (NSString *)exportResultsAsJSON {
    if (_result.count() == 0)
        return nil;

    NSMutableArray *items = [NSMutableArray arrayWithCapacity:_result.count()];
    for (size_t i = 0; i < _result.count(); i++) {
        const uintptr_t address = _result.addresses[i];
        NSMutableDictionary *item = [NSMutableDictionary dictionary];
        item[@"address"] = SYFormatAddress(address);
        item[@"type"] = _searchType;

        // Plain format, not the display form: exported values must be
        // re-parseable, and the display form annotates integers with hex.
        const uint8_t *snapshot = _result.snapshotAt(i);
        if (snapshot)
            item[@"value"] = SYValueTypeUtil::formatValue(snapshot, _searchType);
        [items addObject:item];
    }

    NSError *err = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:items
                                                   options:NSJSONWritingPrettyPrinted
                                                     error:&err];
    if (!data || err)
        return nil;

    NSArray *paths =
        NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *docs = paths.firstObject;
    if (!docs.length)
        return nil;

    NSString *dir = [docs stringByAppendingPathComponent:@"Shirayuki"];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:&err]) {
        return nil;
    }

    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyyMMdd-HHmmss";
    NSString *filename =
        [NSString stringWithFormat:@"results_%@.json", [formatter stringFromDate:[NSDate date]]];
    NSString *path = [dir stringByAppendingPathComponent:filename];

    // The write result decides the return value: reporting a path for a file
    // that was never created told the user their export had succeeded.
    if (![data writeToFile:path atomically:YES])
        return nil;
    return path;
}

#pragma mark - Tab hooks

- (BOOL)showsNarrowBar {
    return self.isNarrowing;
}

- (BOOL)showsInputHistory {
    return YES;
}

- (NSArray<NSString *> *)inputHistory {
    return [self searchHistory];
}

- (NSArray<UIAlertAction *> *)actionButtonMenuActions {
    if (_result.count() == 0)
        return @[];

    __weak __typeof__(self) weakSelf = self;
    NSMutableArray<UIAlertAction *> *actions = [NSMutableArray array];

    [actions addObject:[UIAlertAction actionWithTitle:@"Export to JSON"
                                                style:UIAlertActionStyleDefault
                                              handler:^(UIAlertAction *a) {
                                                  [weakSelf exportAndReport];
                                              }]];

    if ([self isNumericType]) {
        [actions
            addObject:[UIAlertAction actionWithTitle:@"Batch Modify"
                                               style:UIAlertActionStyleDefault
                                             handler:^(UIAlertAction *a) {
                                                 [weakSelf.viewController showBatchModifyAlert];
                                             }]];
    }
    return actions;
}

- (void)exportAndReport {
    NSString *path = [self exportResultsAsJSON];
    if (path) {
        [SYToast show:[NSString stringWithFormat:@"Saved: %@", [path lastPathComponent]]
                 type:SYToastSuccess];
    } else {
        [SYToast show:@"Export failed" type:SYToastError];
    }
}

#pragma mark - Table

- (NSInteger)numberOfRows {
    return (NSInteger)_result.count();
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    const uintptr_t address = _result.addresses[(size_t)row];

    NSString *valueStr = @"—";
    if ([self isNumericType]) {
        uint8_t buf[kMaxValueSize] = {};
        const size_t width = SYValueTypeUtil::sizeOfTag(_searchType);
        // Show the live value, and say so plainly when it can no longer be read
        // rather than rendering a zeroed buffer as a real value.
        if (Memory::read(address, buf, width) == Status::Success)
            valueStr = SYValueTypeUtil::displayValue(buf, _searchType);
        else
            valueStr = @"<unreadable>";
    }

    [cell configureWithIcon:[SYTheme icon:@"memorychip" size:kCellIconSize]
                      title:SYFormatAddress(address)
                     detail:valueStr
                      badge:[self shortType]
                 badgeColor:[SYTheme accentDim]];
}

- (BOOL)canDeleteRow:(NSInteger)row {
    return NO;
}

- (void)didSelectRow:(NSInteger)row {
    if (row < 0 || (size_t)row >= _result.count())
        return;
    if (![self isNumericType]) {
        [self copyAddressToClipboard:_result.addresses[(size_t)row]];
        return;
    }
    [self.viewController showModifyAlertForAddress:_result.addresses[(size_t)row] type:_searchType];
}

- (void)didLongPressRow:(NSInteger)row {
    if (row < 0 || (size_t)row >= _result.count())
        return;
    [self copyAddressToClipboard:_result.addresses[(size_t)row]];
}

@end
