#import <UIKit/UIKit.h>
#import "SYTabHandler.h"

@class ShirayukiViewController;

@interface SYDumpHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
@end
