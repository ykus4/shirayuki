#import "SYThreadHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiViewController.h"
#import "ThreadList.hpp"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYThreadHandler ()
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *rows;
@end

@implementation SYThreadHandler

- (instancetype)init {
    self = [super init];
    if (self)
        _rows = [NSMutableArray new];
    return self;
}

- (NSString *)tabTitle {
    return @"Threads";
}
- (NSString *)tabIcon {
    return @"cpu";
}
- (NSString *)placeholder {
    return @"tap → refresh";
}
- (NSString *)typeLabel {
    return @"thr";
}
- (NSString *)actionIcon {
    return @"arrow.clockwise";
}

- (void)performAction:(NSString *)input {
    [self refresh];
}

- (void)refresh {
    auto threads = ThreadList::all();
    [_rows removeAllObjects];
    for (auto &t : threads) {
        [_rows addObject:@{
            @"tid" : @(t.tid),
            @"pc" : @(t.pc),
            @"sp" : @(t.sp),
            @"lr" : @(t.lr),
            @"state" : @(t.state.c_str())
        }];
    }
    [SYToast show:[NSString stringWithFormat:@"%zu threads", threads.size()] type:SYToastInfo];
    [self.viewController reloadTable];
}

- (NSInteger)numberOfRows {
    return _rows.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];
    NSDictionary *e = _rows[row];
    NSString *title = [NSString stringWithFormat:@"tid %llu", [e[@"tid"] unsignedLongLongValue]];
    NSString *detail =
        [NSString stringWithFormat:@"pc=0x%llX  sp=0x%llX", [e[@"pc"] unsignedLongLongValue],
                                   [e[@"sp"] unsignedLongLongValue]];
    [cell configureWithIcon:[SYTheme icon:@"cpu.fill" size:14 color:[SYTheme accent]]
                      title:title
                     detail:detail
                      badge:e[@"state"]
                 badgeColor:[SYTheme accentDim]];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSDictionary *e = _rows[row];
    NSString *pc = [NSString stringWithFormat:@"0x%llX", [e[@"pc"] unsignedLongLongValue]];
    [UIPasteboard generalPasteboard].string = pc;
    [SYToast show:[NSString stringWithFormat:@"Copied %@", pc] type:SYToastInfo];
}

@end
