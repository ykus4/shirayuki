#import "SYSearchHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";
static const size_t kMaxScanResults = 2000;
static const size_t kMaxRegionSize = 100 * 1024 * 1024;

// C++ helper: scan a single region and return matching addresses.
// Keeps all template instantiations out of nested ObjC blocks.
static std::vector<uintptr_t> scanRegion(uintptr_t start, size_t len, NSString *type,
                                         NSString *input, size_t &outValSize) {
    outValSize = 4;
    if ([type isEqualToString:@"int32"]) {
        return Scanner::findValue<int32_t>(start, len, (int32_t)[input intValue]);
    } else if ([type isEqualToString:@"int16"]) {
        outValSize = 2;
        return Scanner::findValue<int16_t>(start, len, (int16_t)[input intValue]);
    } else if ([type isEqualToString:@"int64"]) {
        outValSize = 8;
        return Scanner::findValue<int64_t>(start, len, (int64_t)[input longLongValue]);
    } else if ([type isEqualToString:@"float"]) {
        return Scanner::findValue<float>(start, len, [input floatValue]);
    } else if ([type isEqualToString:@"double"]) {
        outValSize = 8;
        return Scanner::findValue<double>(start, len, [input doubleValue]);
    } else if ([type isEqualToString:@"hex"]) {
        outValSize = 0;
        return Scanner::findPattern(start, len, [input UTF8String]);
    } else if ([type isEqualToString:@"regex"]) {
        outValSize = 0;
        return Scanner::findRegex(start, len, [input UTF8String]);
    } else {
        outValSize = [input lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        return Scanner::findString(start, len, [input UTF8String]);
    }
}

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
    [self performSearch:input];
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
    __weak typeof(self) weakSelf = self;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // Build filtered region list in C++ land, convert to plain ObjC arrays
        auto regions = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
        NSMutableArray *regionStarts = [NSMutableArray arrayWithCapacity:regions.size()];
        NSMutableArray *regionSizes = [NSMutableArray arrayWithCapacity:regions.size()];
        for (auto &r : regions) {
            if (r.size <= kMaxRegionSize) {
                [regionStarts addObject:@(r.start)];
                [regionSizes addObject:@(r.size)];
            }
        }

        NSLock *lock = [NSLock new];
        NSMutableArray *mergedResults = [NSMutableArray new];
        NSMutableArray *mergedCandidates = [NSMutableArray new];
        __block size_t totalHits = 0;
        __block BOOL limitReached = NO;

        dispatch_queue_t concurrentQ = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
        dispatch_group_t group = dispatch_group_create();

        for (NSUInteger ri = 0; ri < regionStarts.count; ri++) {
            uintptr_t rStart = [regionStarts[ri] unsignedLongLongValue];
            size_t rSize = (size_t)[regionSizes[ri] unsignedLongLongValue];
            dispatch_group_async(group, concurrentQ, ^{
                [lock lock];
                BOOL skip = limitReached;
                [lock unlock];
                if (skip)
                    return;

                // All template calls go through the static helper — no templates in this block
                size_t valSize = 4;
                std::vector<uintptr_t> hits = scanRegion(rStart, rSize, searchType, input, valSize);
                if (hits.empty())
                    return;

                NSMutableArray *localR = [NSMutableArray arrayWithCapacity:hits.size()];
                NSMutableArray *localC = [NSMutableArray arrayWithCapacity:hits.size()];
                for (size_t k = 0; k < hits.size(); k++) {
                    uintptr_t addr = hits[k];
                    [localR addObject:@(addr)];
                    NSMutableDictionary *c = [NSMutableDictionary new];
                    c[@"address"] = @(addr);
                    if (valSize > 0) {
                        uint8_t buf[8] = {};
                        Memory::read(addr, buf, valSize);
                        c[@"snapshot"] = [NSData dataWithBytes:buf length:valSize];
                    }
                    [localC addObject:c];
                }

                [lock lock];
                totalHits += hits.size();
                NSUInteger canAdd = kMaxScanResults > mergedResults.count
                                        ? kMaxScanResults - mergedResults.count
                                        : 0;
                if (canAdd > 0) {
                    NSUInteger add = MIN(canAdd, localR.count);
                    [mergedResults
                        addObjectsFromArray:[localR subarrayWithRange:NSMakeRange(0, add)]];
                    [mergedCandidates
                        addObjectsFromArray:[localC subarrayWithRange:NSMakeRange(0, add)]];
                }
                if (totalHits >= kMaxScanResults)
                    limitReached = YES;
                [lock unlock];
            });
        }

        dispatch_group_notify(group, dispatch_get_main_queue(), ^{
            __strong typeof(weakSelf) strongSelf = weakSelf;
            if (!strongSelf)
                return;
            [strongSelf.results setArray:mergedResults];
            [strongSelf.candidates setArray:mergedCandidates];
            strongSelf.searching = NO;
            strongSelf.hasResults = (totalHits > 0);
            strongSelf.isNarrowing = strongSelf.hasResults;
            NSString *msg = [NSString stringWithFormat:@"%zu results", totalHits];
            [SYToast show:msg type:totalHits > 0 ? SYToastSuccess : SYToastWarning];
            [strongSelf.viewController reloadTable];
        });
    });
}

- (void)narrow:(NSString *)mode {
    __weak typeof(self) weakSelf = self;
    NSArray *snapshot = [_candidates copy];
    size_t valSize = [self currentValueSize];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableArray *kept = [NSMutableArray new];

        for (NSMutableDictionary *c in snapshot) {
            uintptr_t addr = [c[@"address"] unsignedLongLongValue];
            NSData *prev = c[@"snapshot"];
            if (!prev)
                continue;

            uint8_t current[8] = {};
            if (Memory::read(addr, current, valSize) != Status::Success)
                continue;

            const uint8_t *prevBytes = (const uint8_t *)prev.bytes;
            BOOL keep = NO;
            if ([mode isEqualToString:@"changed"]) {
                keep = (memcmp(current, prevBytes, valSize) != 0);
            } else if ([mode isEqualToString:@"unchanged"]) {
                keep = (memcmp(current, prevBytes, valSize) == 0);
            } else if ([mode isEqualToString:@"increased"]) {
                keep = (memcmp(current, prevBytes, valSize) > 0);
            } else if ([mode isEqualToString:@"decreased"]) {
                keep = (memcmp(current, prevBytes, valSize) < 0);
            }

            if (keep) {
                c[@"snapshot"] = [NSData dataWithBytes:current length:valSize];
                [kept addObject:c];
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            __strong typeof(weakSelf) strongSelf = weakSelf;
            if (!strongSelf)
                return;
            [strongSelf.candidates setArray:kept];
            [strongSelf.results removeAllObjects];
            for (NSDictionary *c in kept)
                [strongSelf.results addObject:c[@"address"]];
            [SYToast show:[NSString stringWithFormat:@"Narrowed to %lu", (unsigned long)kept.count]
                     type:SYToastInfo];
            [strongSelf.viewController reloadTable];
        });
    });
}

- (void)narrowExact:(NSString *)input {
    size_t valSize = [self currentValueSize];
    uint8_t targetBuf[8] = {};
    SYValueTypeUtil::parseValue(input, _searchType, targetBuf);
    NSData *targetData = [NSData dataWithBytes:targetBuf length:valSize];

    __weak typeof(self) weakSelf = self;
    NSArray *snapshot = [_candidates copy];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableArray *kept = [NSMutableArray new];
        const void *target = targetData.bytes;

        for (NSMutableDictionary *c in snapshot) {
            uintptr_t addr = [c[@"address"] unsignedLongLongValue];
            uint8_t current[8] = {};
            if (Memory::read(addr, current, valSize) != Status::Success)
                continue;
            if (memcmp(current, target, valSize) == 0) {
                c[@"snapshot"] = [NSData dataWithBytes:current length:valSize];
                [kept addObject:c];
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            __strong typeof(weakSelf) strongSelf = weakSelf;
            if (!strongSelf)
                return;
            [strongSelf.candidates setArray:kept];
            [strongSelf.results removeAllObjects];
            for (NSDictionary *c in kept)
                [strongSelf.results addObject:c[@"address"]];
            [SYToast show:[NSString stringWithFormat:@"Exact: %lu", (unsigned long)kept.count]
                     type:SYToastInfo];
            [strongSelf.viewController reloadTable];
        });
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
