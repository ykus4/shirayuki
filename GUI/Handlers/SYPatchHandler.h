#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYPatchHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
- (void)restoreAll;
@end
