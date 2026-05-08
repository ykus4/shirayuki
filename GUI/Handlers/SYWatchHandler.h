#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYWatchHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
- (void)removeAll;
@end
