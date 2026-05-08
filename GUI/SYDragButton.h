#import <UIKit/UIKit.h>

@interface SYDragButton : UIButton
@property (nonatomic, copy) void (^onTap)(void);
@end
