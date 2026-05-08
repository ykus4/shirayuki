#import <UIKit/UIKit.h>

@protocol SYTabHandler <NSObject>
@required
- (NSString *)tabTitle;
- (NSString *)tabIcon; // SF Symbol name
- (NSString *)placeholder;
- (NSString *)typeLabel;
- (NSString *)actionIcon; // SF Symbol for action button

- (void)performAction:(NSString *)input;
- (NSInteger)numberOfRows;
- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView;

@optional
- (void)didSelectRow:(NSInteger)row;
- (BOOL)canDeleteRow:(NSInteger)row;
- (void)deleteRow:(NSInteger)row;
- (void)didLongPressRow:(NSInteger)row;
- (UISwipeActionsConfiguration *)trailingSwipeForRow:(NSInteger)row;
- (CGFloat)rowHeight;
@end
