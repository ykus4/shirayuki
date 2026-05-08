#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYFreezeHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
- (void)removeAll;
- (void)toggleAutoIncrementForRow:(NSInteger)row;
@end
