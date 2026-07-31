#import "SYSearchHandler.h"
#import "SYDispatchUtil.h"
#import "SYResultCell.h"
#import "SYScanHelper.hpp"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYSearchHandler ()
@property (nonatomic, strong) NSMutableArray<NSNumber *> *results;
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *candidates;
@property (nonatomic, strong) NSMutableArray<NSString *> *history;
@property (nonatomic, assign) BOOL searching;
@end

@implementation SYSearchHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _results = [NSMutableArray new];
        _candidates = [NSMutableArray new];
        _history = [NSMutableArray new];
        _searchType = @"int32";
        _hasResults = NO;
        _isNarrowing = NO;
    }
    return self;
}

#pragma mark - SYTabHandler

- (NSString *)tabTitle {
    return @"Search";
}
- (NSString *)tabIcon {
    return @"magnifyingglass";
}
- (NSString *)placeholder {
    return _isNarrowing ? @"New value or leave empty for filter..."
                        : @"Value, pattern, or string...";
}
- (NSString *)typeLabel {
    return [self shortType];
}
- (NSString *)actionIcon {
    return _isNarrowing ? @"line.3.horizontal.decrease" : @"play.fill";
}

- (NSString *)shortType {
    if ([_searchType isEqualToString:@"hex"])
        return @"hex";
    if ([_searchType isEqualToString:@"string"])
        return @"str";
    if ([_searchType isEqualToString:@"regex"])
        return @"rex";
    return SYValueTypeUtil::shortLabel(SYValueTypeUtil::fromString(_searchType));
}

- (void)cycleType {
    NSArray *types =
        @[ @"int32", @"int16", @"int64", @"float", @"double", @"hex", @"string", @"regex" ];
    NSUInteger idx = [types indexOfObject:_searchType];
    _searchType = types[(idx + 1) % types.count];
}

- (void)performAction:(NSString *)input {
    if (_isNarrowing) {
        if (!input.length) {
            [self narrow:@"changed"];
        } else {
            [self narrowExact:input];
        }
        return;
    }
    // "?" or empty input on a numeric type starts an unknown-initial-value scan —
    // it seeds candidates over all readable regions without a comparison target,
    // so the user can subsequently narrow via changed/inc/dec.
    if ([input isEqualToString:@"?"] || (!input.length && [self isNumericType])) {
        [self performUnknownScan];
        return;
    }
    [self performSearch:input];
}

- (BOOL)isNumericType {
    return ![_searchType isEqualToString:@"hex"] && ![_searchType isEqualToString:@"string"] &&
           ![_searchType isEqualToString:@"regex"];
}

- (void)performUnknownScan {
    if (_searching)
        return;
    _searching = YES;
    [_results removeAllObjects];
    [_candidates removeAllObjects];

    ValueType vtype = SYValueTypeUtil::fromString(_searchType);
    __block NSMutableArray *localResults = nil;
    __block NSMutableArray *localCandidates = nil;

    SYAsync(
        ^{
            auto regions = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
            size_t remaining = kMaxScanResults;
            localResults = [NSMutableArray new];
            localCandidates = [NSMutableArray new];

            for (auto &r : regions) {
                if (!remaining)
                    break;
                if (r.size > kMaxRegionSize)
                    continue;
                auto seeded = Scanner::seedUnknownCandidates(r.start, r.size, vtype, remaining);
                for (auto &c : seeded) {
                    [localResults addObject:@((uint64_t)c.address)];
                    NSMutableDictionary *d = [NSMutableDictionary new];
                    d[@"address"] = @((uint64_t)c.address);
                    d[@"snapshot"] = [NSData dataWithBytes:c.snapshotValue.data()
                                                    length:c.snapshotValue.size()];
                    [localCandidates addObject:d];
                }
                if (seeded.size() >= remaining) {
                    remaining = 0;
                } else {
                    remaining -= seeded.size();
                }
            }
        },
        ^{
            [self.results setArray:localResults];
            [self.candidates setArray:localCandidates];
            self.searching = NO;
            self.hasResults = (self.results.count > 0);
            self.isNarrowing = self.hasResults;
            [SYToast show:[NSString stringWithFormat:@"Unknown seeded: %lu",
                                                     (unsigned long)self.results.count]
                     type:SYToastInfo];
            [self.viewController reloadTable];
        });
}

- (void)performSearch:(NSString *)input {
    if (_searching)
        return;
    _searching = YES;
    [_results removeAllObjects];
    [_candidates removeAllObjects];

    if (input.length && ![_history containsObject:input]) {
        [_history insertObject:input atIndex:0];
        if (_history.count > 20)
            [_history removeLastObject];
    }

    NSString *searchType = _searchType;

    __block NSMutableArray *localResults = nil;
    __block NSMutableArray *localCandidates = nil;
    __block size_t count = 0;

    SYAsync(
        ^{
            // SYScanAll is a plain C function — no C++ syntax in this block
            size_t valSize = 4;
            uintptr_t *hits = SYScanAll([searchType UTF8String], [input UTF8String],
                                        kMaxScanResults, kMaxRegionSize, &count, &valSize);

            localResults = [NSMutableArray arrayWithCapacity:count];
            localCandidates = [NSMutableArray arrayWithCapacity:count];

            for (size_t k = 0; k < count; k++) {
                uintptr_t addr = hits[k];
                [localResults addObject:@(addr)];
                NSMutableDictionary *c = [NSMutableDictionary new];
                c[@"address"] = @(addr);
                if (valSize > 0) {
                    unsigned char buf[8] = {};
                    SYMemRead(addr, buf, valSize);
                    c[@"snapshot"] = [NSData dataWithBytes:buf length:valSize];
                }
                [localCandidates addObject:c];
            }
            SYScanFreeResults(hits);
        },
        ^{
            [self.results setArray:localResults];
            [self.candidates setArray:localCandidates];
            self.searching = NO;
            self.hasResults = (count > 0);
            self.isNarrowing = self.hasResults;
            NSString *msg = [NSString stringWithFormat:@"%zu results", count];
            [SYToast show:msg type:count > 0 ? SYToastSuccess : SYToastWarning];
            [self.viewController reloadTable];
        });
}

- (void)narrow:(NSString *)mode {
    [self narrowWithMode:mode target:nil toastPrefix:@"Narrowed to"];
}

- (void)narrowExact:(NSString *)input {
    size_t valSize = [self currentValueSize];
    uint8_t targetBuf[8] = {};
    SYValueTypeUtil::parseValue(input, _searchType, targetBuf);
    NSData *targetData = [NSData dataWithBytes:targetBuf length:valSize];
    [self narrowWithMode:@"exact" target:targetData toastPrefix:@"Exact:"];
}

// Unified narrow: `target` is nil for changed/unchanged/increased/decreased,
// non-nil for the "exact match" variant. Result publishing is identical for both.
- (void)narrowWithMode:(NSString *)mode
                target:(NSData *)target
           toastPrefix:(NSString *)toastPrefix {
    NSArray *snapshot = [_candidates copy];
    size_t valSize = [self currentValueSize];
    ValueType vtype = SYValueTypeUtil::fromString(_searchType);
    __block NSMutableArray *kept = nil;

    SYAsync(
        ^{
            kept = [NSMutableArray new];
            const uint8_t *targetBytes = target ? (const uint8_t *)target.bytes : NULL;

            for (NSMutableDictionary *c in snapshot) {
                uintptr_t addr = [c[@"address"] unsignedLongLongValue];
                uint8_t current[8] = {};
                if (Memory::read(addr, current, valSize) != Status::Success)
                    continue;

                BOOL keep = NO;
                if (targetBytes) {
                    keep = (memcmp(current, targetBytes, valSize) == 0);
                } else {
                    NSData *prev = c[@"snapshot"];
                    if (!prev)
                        continue;
                    const uint8_t *prevBytes = (const uint8_t *)prev.bytes;
                    if ([mode isEqualToString:@"changed"])
                        keep = (memcmp(current, prevBytes, valSize) != 0);
                    else if ([mode isEqualToString:@"unchanged"])
                        keep = (memcmp(current, prevBytes, valSize) == 0);
                    else if ([mode isEqualToString:@"increased"])
                        keep = (compareTypedBytes(current, prevBytes, vtype) > 0);
                    else if ([mode isEqualToString:@"decreased"])
                        keep = (compareTypedBytes(current, prevBytes, vtype) < 0);
                }

                if (keep) {
                    c[@"snapshot"] = [NSData dataWithBytes:current length:valSize];
                    [kept addObject:c];
                }
            }
        },
        ^{
            [self.candidates setArray:kept];
            [self.results removeAllObjects];
            for (NSDictionary *c in kept)
                [self.results addObject:c[@"address"]];
            [SYToast
                show:[NSString stringWithFormat:@"%@ %lu", toastPrefix, (unsigned long)kept.count]
                type:SYToastInfo];
            [self.viewController reloadTable];
        });
}

- (size_t)currentValueSize {
    if ([_searchType isEqualToString:@"int16"])
        return 2;
    if ([_searchType isEqualToString:@"int64"] || [_searchType isEqualToString:@"double"])
        return 8;
    return 4;
}

- (void)batchModify:(NSString *)value {
    size_t valSize = [self currentValueSize];
    uint8_t buf[8] = {};
    SYValueTypeUtil::parseValue(value, _searchType, buf);

    size_t count = 0;
    for (NSNumber *addr in _results) {
        if (Memory::write([addr unsignedLongLongValue], buf, valSize) == Status::Success)
            count++;
    }
    [SYToast show:[NSString stringWithFormat:@"Modified %zu addresses", count] type:SYToastSuccess];
}

- (void)resetSearch {
    [_results removeAllObjects];
    [_candidates removeAllObjects];
    _hasResults = NO;
    _isNarrowing = NO;
    [self.viewController reloadTable];
}

- (NSArray<NSString *> *)searchHistory {
    return [_history copy];
}

- (NSString *)exportResultsAsJSON {
    if (!_results.count)
        return nil;

    NSMutableArray *items = [NSMutableArray arrayWithCapacity:_results.count];
    for (NSNumber *addrNum in _results) {
        uintptr_t addr = [addrNum unsignedLongLongValue];
        uint8_t buf[8] = {};
        Memory::read(addr, buf, [self currentValueSize]);
        NSString *val = SYValueTypeUtil::formatValue(buf, _searchType);
        [items addObject:@{
            @"address" : [NSString stringWithFormat:@"0x%lX", addr],
            @"value" : val,
            @"type" : _searchType
        }];
    }

    NSError *err = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:items
                                                   options:NSJSONWritingPrettyPrinted
                                                     error:&err];
    if (!data || err)
        return nil;

    // Save to Documents/Shirayuki/
    NSArray *paths =
        NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *docs = paths.firstObject;
    NSString *dir = [docs stringByAppendingPathComponent:@"Shirayuki"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSTimeInterval ts = [[NSDate date] timeIntervalSince1970];
    NSString *filename = [NSString stringWithFormat:@"results_%lld.json", (long long)ts];
    NSString *path = [dir stringByAppendingPathComponent:filename];
    [data writeToFile:path atomically:YES];
    return path;
}

#pragma mark - Table

- (NSInteger)numberOfRows {
    return _results.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    uintptr_t addr = [_results[row] unsignedLongLongValue];
    uint8_t buf[8] = {};
    Memory::read(addr, buf, [self currentValueSize]);
    NSString *valueStr = SYValueTypeUtil::formatValue(buf, _searchType);

    [cell configureWithIcon:[SYTheme icon:@"memorychip" size:14]
                      title:[NSString stringWithFormat:@"0x%lX", addr]
                     detail:valueStr
                      badge:[self shortType]
                 badgeColor:[SYTheme accentDim]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    uintptr_t addr = [_results[row] unsignedLongLongValue];
    [self.viewController showModifyAlertForAddress:addr type:_searchType];
}

- (void)didLongPressRow:(NSInteger)row {
    uintptr_t addr = [_results[row] unsignedLongLongValue];
    NSString *addrStr = [NSString stringWithFormat:@"0x%lX", addr];
    [UIPasteboard generalPasteboard].string = addrStr;
    [SYToast show:[NSString stringWithFormat:@"Copied %@", addrStr] type:SYToastInfo];
}

@end
