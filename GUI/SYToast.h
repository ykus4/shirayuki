#import <UIKit/UIKit.h>

typedef NS_ENUM(NSInteger, SYToastType) {
    SYToastSuccess,
    SYToastError,
    SYToastWarning,
    SYToastInfo
};

@interface SYToast : NSObject
+ (void)show:(NSString *)message type:(SYToastType)type;
+ (void)show:(NSString *)message type:(SYToastType)type duration:(NSTimeInterval)duration;
+ (void)showInView:(UIView *)view message:(NSString *)message type:(SYToastType)type;
@end
