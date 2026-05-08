#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYPointerHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
@end
