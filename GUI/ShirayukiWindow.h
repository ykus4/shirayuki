#import <UIKit/UIKit.h>

@interface ShirayukiWindow : UIWindow
+ (instancetype)shared;
- (void)show;
- (void)hide;
- (void)toggle;
@end
