#import <UIKit/UIKit.h>
#import "SYTabHandler.h"

@class ShirayukiViewController;

@interface SYPatchHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
- (void)restoreAll;
@end
