#import "SYModuleHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYModuleHandler ()
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *rows;
@property (nonatomic, copy) NSString *filter;
@end

@implementation SYModuleHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _rows = [NSMutableArray new];
        [self refresh];
    }
    return self;
}

- (NSString *)tabTitle {
    return @"Modules";
}
- (NSString *)tabIcon {
    return @"square.stack.3d.up";
}
- (NSString *)placeholder {
    return @"Filter substring...";
}
- (NSString *)typeLabel {
    return @"mod";
}
- (NSString *)actionIcon {
    return @"arrow.clockwise";
}

- (void)performAction:(NSString *)input {
    _filter = input;
    [self refresh];
}

- (void)refresh {
    auto images = Image::listAll();
    [_rows removeAllObjects];

    for (auto &img : images) {
        NSString *name = @(img.name.c_str());
        if (_filter.length &&
            [name rangeOfString:_filter options:NSCaseInsensitiveSearch].location == NSNotFound)
            continue;

        [_rows addObject:@{@"name" : name, @"base" : @(img.base), @"slide" : @(img.slide)}];
    }
    [SYToast show:[NSString stringWithFormat:@"%lu modules", (unsigned long)_rows.count]
             type:SYToastInfo];
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
    NSString *full = e[@"name"];
    NSString *shortName = [full lastPathComponent];
    NSString *detail =
        [NSString stringWithFormat:@"base=0x%llX  slide=0x%llX", [e[@"base"] unsignedLongLongValue],
                                   [e[@"slide"] unsignedLongLongValue]];

    [cell configureWithIcon:[SYTheme icon:@"square.stack.3d.up.fill" size:14 color:[SYTheme accent]]
                      title:shortName
                     detail:detail
                      badge:nil
                 badgeColor:nil];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSDictionary *e = _rows[row];
    NSString *base = [NSString stringWithFormat:@"0x%llX", [e[@"base"] unsignedLongLongValue]];
    [UIPasteboard generalPasteboard].string = base;
    [SYToast show:[NSString stringWithFormat:@"Base %@", base] type:SYToastInfo];
}

- (void)didLongPressRow:(NSInteger)row {
    // Long-press copies full path — useful when driving Scanner::findPatternInImage.
    NSDictionary *e = _rows[row];
    [UIPasteboard generalPasteboard].string = e[@"name"];
    [SYToast show:@"Path copied" type:SYToastInfo];
}

@end
