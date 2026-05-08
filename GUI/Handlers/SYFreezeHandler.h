#import <UIKit/UIKit.h>
#import "SYTabHandler.h"

@class ShirayukiViewController;

@interface SYFreezeHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
- (void)removeAll;
@end
