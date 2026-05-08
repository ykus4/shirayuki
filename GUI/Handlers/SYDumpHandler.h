#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYDumpHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
@end
