#import <UIKit/UIKit.h>

/// Contract between ShirayukiViewController and a tab.
///
/// The optional hooks exist so the view controller never needs to know *which*
/// tab is selected. It previously branched on `_currentTabIndex == 0/1/2` in six
/// places and reached into `_handlers[0..3]` by index — behind accessors
/// commented "safe against reordering", which they were not.
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
- (UISwipeActionsConfiguration *)trailingSwipeForRow:(NSInteger)row;
- (CGFloat)rowHeight;

/// Tapping the type button cycles the tab's value type. Tabs without a type
/// simply do not implement this.
- (void)cycleType;

/// Actions for a long press on the action button, shown as an action sheet.
/// Return nil or an empty array for no menu.
- (NSArray<UIAlertAction *> *)actionButtonMenuActions;

/// Actions for a long press on a row. Returning a non-empty array shows an
/// action sheet; returning nil falls back to `didLongPressRow:`.
- (NSArray<UIAlertAction *> *)contextActionsForRow:(NSInteger)row;

/// Long press on a row, when no context menu is offered.
- (void)didLongPressRow:(NSInteger)row;

/// Whether the narrowing filter bar should be visible right now.
- (BOOL)showsNarrowBar;

/// Whether focusing the input field should offer the history dropdown.
- (BOOL)showsInputHistory;

/// Previously-entered inputs, most recent first. Required when
/// `showsInputHistory` returns YES.
- (NSArray<NSString *> *)inputHistory;
@end
