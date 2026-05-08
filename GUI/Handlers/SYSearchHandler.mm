#import "SYSearchHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYSearchHandler ()
@property (nonatomic, strong) NSMutableArray<NSNumber *> *results;
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *candidates; // for narrowing
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
    if ([_searchType isEqualToString:@"int32"])
        return @"i32";
    if ([_searchType isEqualToString:@"int64"])
        return @"i64";
    if ([_searchType isEqualToString:@"int16"])
        return @"i16";
    if ([_searchType isEqualToString:@"float"])
        return @"f32";
    if ([_searchType isEqualToString:@"double"])
        return @"f64";
    if ([_searchType isEqualToString:@"hex"])
        return @"hex";
    if ([_searchType isEqualToString:@"string"])
        return @"str";
    return @"i32";
}

- (void)cycleType {
    NSArray *types = @[ @"int32", @"int16", @"int64", @"float", @"double", @"hex", @"string" ];
    NSUInteger idx = [types indexOfObject:_searchType];
    _searchType = types[(idx + 1) % types.count];
}

- (void)performAction:(NSString *)input {
    if (_isNarrowing) {
        // If input is empty and we're narrowing, this is a "changed" filter by default
        if (!input.length) {
            [self narrow:@"changed"];
        } else {
            [self narrow:@"exact"];
            // Actually narrow with exact value
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

    // Add to history
    if (input.length && ![_history containsObject:input]) {
        [_history insertObject:input atIndex:0];
        if (_history.count > 20)
            [_history removeLastObject];
    }

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto regions = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
        size_t totalHits = 0;

        for (auto &region : regions) {
            if (region.size > 100 * 1024 * 1024)
                continue;

            std::vector<uintptr_t> hits;
            size_t valSize = 4;

            if ([self.searchType isEqualToString:@"int32"]) {
                int32_t val = [input intValue];
                hits = Scanner::findValue<int32_t>(region.start, region.size, val);
                valSize = 4;
            } else if ([self.searchType isEqualToString:@"int16"]) {
                int16_t val = (int16_t)[input intValue];
                hits = Scanner::findValue<int16_t>(region.start, region.size, val);
                valSize = 2;
            } else if ([self.searchType isEqualToString:@"int64"]) {
                int64_t val = [input longLongValue];
                hits = Scanner::findValue<int64_t>(region.start, region.size, val);
                valSize = 8;
            } else if ([self.searchType isEqualToString:@"float"]) {
                float val = [input floatValue];
                hits = Scanner::findValue<float>(region.start, region.size, val);
                valSize = 4;
            } else if ([self.searchType isEqualToString:@"double"]) {
                double val = [input doubleValue];
                hits = Scanner::findValue<double>(region.start, region.size, val);
                valSize = 8;
            } else if ([self.searchType isEqualToString:@"hex"]) {
                hits = Scanner::findPattern(region.start, region.size, [input UTF8String]);
                valSize = 0; // variable
            } else {
                hits = Scanner::findString(region.start, region.size, [input UTF8String]);
                valSize = [input lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
            }

            for (auto addr : hits) {
                if (totalHits < 2000) {
                    [self.results addObject:@(addr)];

                    // Store candidate for narrowing
                    NSMutableDictionary *c = [NSMutableDictionary new];
                    c[@"address"] = @(addr);
                    if (valSize > 0) {
                        uint8_t buf[8] = {};
                        Memory::read(addr, buf, valSize);
                        c[@"snapshot"] = [NSData dataWithBytes:buf length:valSize];
                    }
                    [self.candidates addObject:c];
                }
                totalHits++;
            }
            if (totalHits >= 2000)
                break;
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            self.searching = NO;
            self.hasResults = (totalHits > 0);
            self.isNarrowing = self.hasResults;

            NSString *msg = [NSString stringWithFormat:@"%zu results", totalHits];
            [SYToast show:msg type:totalHits > 0 ? SYToastSuccess : SYToastWarning];
            [self.viewController reloadTable];
        });
    });
}

- (void)narrow:(NSString *)mode {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableArray *kept = [NSMutableArray new];
        size_t valSize = [self currentValueSize];

        for (NSMutableDictionary *c in self.candidates) {
            uintptr_t addr = [c[@"address"] unsignedLongLongValue];
            NSData *snapshot = c[@"snapshot"];
            if (!snapshot)
                continue;

            uint8_t current[8] = {};
            if (Memory::read(addr, current, valSize) != Status::Success)
                continue;

            BOOL keep = NO;
            const uint8_t *prev = (const uint8_t *)snapshot.bytes;

            if ([mode isEqualToString:@"changed"]) {
                keep = (memcmp(current, prev, valSize) != 0);
            } else if ([mode isEqualToString:@"unchanged"]) {
                keep = (memcmp(current, prev, valSize) == 0);
            } else if ([mode isEqualToString:@"increased"]) {
                keep = (memcmp(current, prev, valSize) > 0); // simplified
            } else if ([mode isEqualToString:@"decreased"]) {
                keep = (memcmp(current, prev, valSize) < 0);
            }

            if (keep) {
                c[@"snapshot"] = [NSData dataWithBytes:current length:valSize];
                [kept addObject:c];
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.candidates setArray:kept];
            [self.results removeAllObjects];
            for (NSDictionary *c in kept) {
                [self.results addObject:c[@"address"]];
            }
            NSString *msg =
                [NSString stringWithFormat:@"Narrowed to %lu", (unsigned long)kept.count];
            [SYToast show:msg type:SYToastInfo];
            [self.viewController reloadTable];
        });
    });
}

- (void)narrowExact:(NSString *)input {
    size_t valSize = [self currentValueSize];
    uint8_t target[8] = {};

    if ([_searchType isEqualToString:@"int32"]) {
        int32_t v = [input intValue];
        memcpy(target, &v, 4);
    } else if ([_searchType isEqualToString:@"float"]) {
        float v = [input floatValue];
        memcpy(target, &v, 4);
    } else if ([_searchType isEqualToString:@"int64"]) {
        int64_t v = [input longLongValue];
        memcpy(target, &v, 8);
    } else if ([_searchType isEqualToString:@"double"]) {
        double v = [input doubleValue];
        memcpy(target, &v, 8);
    }

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableArray *kept = [NSMutableArray new];

        for (NSMutableDictionary *c in self.candidates) {
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
            [self.candidates setArray:kept];
            [self.results removeAllObjects];
            for (NSDictionary *c in kept) {
                [self.results addObject:c[@"address"]];
            }
            [SYToast show:[NSString stringWithFormat:@"Exact: %lu", (unsigned long)kept.count]
                     type:SYToastInfo];
            [self.viewController reloadTable];
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

    if ([_searchType isEqualToString:@"int32"]) {
        int32_t v = [value intValue];
        memcpy(buf, &v, 4);
    } else if ([_searchType isEqualToString:@"float"]) {
        float v = [value floatValue];
        memcpy(buf, &v, 4);
    } else if ([_searchType isEqualToString:@"int64"]) {
        int64_t v = [value longLongValue];
        memcpy(buf, &v, 8);
    } else if ([_searchType isEqualToString:@"double"]) {
        double v = [value doubleValue];
        memcpy(buf, &v, 8);
    }

    size_t count = 0;
    for (NSNumber *addr in _results) {
        if (Memory::write([addr unsignedLongLongValue], buf, valSize) == Status::Success) {
            count++;
        }
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

#pragma mark - Table

- (NSInteger)numberOfRows {
    return _results.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    uintptr_t addr = [_results[row] unsignedLongLongValue];
    NSString *valueStr;

    if ([_searchType isEqualToString:@"int32"]) {
        int32_t v = Memory::readValue<int32_t>(addr);
        valueStr = [NSString stringWithFormat:@"%d (0x%X)", v, v];
    } else if ([_searchType isEqualToString:@"float"]) {
        float v = Memory::readValue<float>(addr);
        valueStr = [NSString stringWithFormat:@"%.3f", v];
    } else if ([_searchType isEqualToString:@"int64"]) {
        int64_t v = Memory::readValue<int64_t>(addr);
        valueStr = [NSString stringWithFormat:@"%lld", v];
    } else if ([_searchType isEqualToString:@"double"]) {
        double v = Memory::readValue<double>(addr);
        valueStr = [NSString stringWithFormat:@"%.5f", v];
    } else if ([_searchType isEqualToString:@"int16"]) {
        int16_t v = Memory::readValue<int16_t>(addr);
        valueStr = [NSString stringWithFormat:@"%d", v];
    } else {
        int32_t v = Memory::readValue<int32_t>(addr);
        valueStr = [NSString stringWithFormat:@"%d (0x%X)", v, v];
    }

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
