#import <UIKit/UIKit.h>
#import "SYTabHandler.h"

@class ShirayukiViewController;

@interface SYPointerHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
@end
