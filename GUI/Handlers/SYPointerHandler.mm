#import "SYPointerHandler.h"
#import "PointerScan.hpp"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYPointerHandler ()
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *results;
@end

@implementation SYPointerHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _results = [NSMutableArray new];
    }
    return self;
}

- (NSString *)tabTitle {
    return @"Ptr";
}
- (NSString *)tabIcon {
    return @"arrow.triangle.branch";
}
- (NSString *)placeholder {
    return @"0xTARGET [depth] [offset]";
}
- (NSString *)typeLabel {
    return @"ptr";
}
- (NSString *)actionIcon {
    return @"magnifyingglass.circle.fill";
}

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    if (!addr) {
        [SYToast show:@"Invalid address" type:SYToastError];
        return;
    }

    uint32_t depth = parts.count > 1 ? [parts[1] intValue] : 3;
    int64_t maxOff = parts.count > 2 ? strtoull([parts[2] UTF8String], NULL, 16) : 0x1000;

    [_results removeAllObjects];
    [SYToast show:@"Scanning pointers..." type:SYToastInfo];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        PointerScanConfig config;
        config.targetAddress = (uintptr_t)addr;
        config.maxDepth = depth;
        config.maxOffset = maxOff;
        config.maxResults = 50;

        auto chains = PointerScanner::scan(config);

        NSMutableArray *localResults = [NSMutableArray new];
        for (auto &chain : chains) {
            uintptr_t resolved = chain.resolve();
            BOOL valid = (resolved == (uintptr_t)addr);
            [localResults addObject:@{
                @"desc" : @(chain.toString().c_str()),
                @"valid" : @(valid),
                @"depth" : @(chain.offsets.size())
            }];
        }
        size_t chainCount = chains.size();

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.results setArray:localResults];
            [SYToast show:[NSString stringWithFormat:@"%zu chains", chainCount]
                     type:SYToastSuccess];
            [self.viewController reloadTable];
        });
    });
}

- (NSInteger)numberOfRows {
    return _results.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    NSDictionary *entry = _results[row];
    BOOL valid = [entry[@"valid"] boolValue];

    [cell configureWithIcon:[SYTheme icon:@"arrow.triangle.branch"
                                     size:14
                                    color:valid ? [SYTheme success] : [SYTheme warning]]
                      title:entry[@"desc"]
                     detail:[NSString stringWithFormat:@"Depth %@", entry[@"depth"]]
                      badge:valid ? @"OK" : @"??"
                 badgeColor:valid ? [SYTheme success] : [SYTheme warning]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSString *desc = _results[row][@"desc"];
    [UIPasteboard generalPasteboard].string = desc;
    [SYToast show:@"Chain copied" type:SYToastInfo];
}

- (void)didLongPressRow:(NSInteger)row {
    [self didSelectRow:row];
}

@end
