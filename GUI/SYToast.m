#import "SYToast.h"
#import "SYTheme.h"

static const CGFloat kToastHeight = 36;
static const NSTimeInterval kDefaultDuration = 2.0;

@implementation SYToast

+ (void)show:(NSString *)message type:(SYToastType)type {
    [self show:message type:type duration:kDefaultDuration];
}

+ (void)show:(NSString *)message type:(SYToastType)type duration:(NSTimeInterval)duration {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIWindow *keyWindow = nil;
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if ([scene isKindOfClass:[UIWindowScene class]]) {
                for (UIWindow *w in ((UIWindowScene *)scene).windows) {
                    if (w.isKeyWindow) { keyWindow = w; break; }
                }
            }
            if (keyWindow) break;
        }
        if (keyWindow) {
            [self showInView:keyWindow message:message type:type];
        }
    });
}

+ (void)showInView:(UIView *)view message:(NSString *)message type:(SYToastType)type {
    CGFloat w = view.bounds.size.width - 32;
    CGFloat startY = view.safeAreaInsets.top + 8;

    UIView *toast = [[UIView alloc] initWithFrame:CGRectMake(16, startY - 40, w, kToastHeight)];
    toast.backgroundColor = [self bgColorForType:type];
    toast.layer.cornerRadius = kToastHeight / 2.0;
    toast.layer.shadowColor = [UIColor blackColor].CGColor;
    toast.layer.shadowOffset = CGSizeMake(0, 2);
    toast.layer.shadowOpacity = 0.4;
    toast.layer.shadowRadius = 8;
    toast.alpha = 0;

    // Icon
    UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake(12, 8, 20, 20)];
    icon.image = [self iconForType:type];
    icon.contentMode = UIViewContentModeScaleAspectFit;
    [toast addSubview:icon];

    // Label
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(38, 0, w - 50, kToastHeight)];
    label.text = message;
    label.font = [SYTheme captionFont];
    label.textColor = [UIColor whiteColor];
    [toast addSubview:label];

    [view addSubview:toast];

    // Animate in
    [UIView animateWithDuration:0.4 delay:0 usingSpringWithDamping:0.8
          initialSpringVelocity:0.5 options:0 animations:^{
        toast.alpha = 1;
        toast.frame = CGRectMake(16, startY, w, kToastHeight);
    } completion:^(BOOL finished) {
        // Animate out
        [UIView animateWithDuration:0.3 delay:kDefaultDuration options:UIViewAnimationOptionCurveEaseIn
                         animations:^{
            toast.alpha = 0;
            toast.transform = CGAffineTransformMakeTranslation(0, -20);
        } completion:^(BOOL finished) {
            [toast removeFromSuperview];
        }];
    }];

    // Haptic
    UIImpactFeedbackGenerator *haptic = [[UIImpactFeedbackGenerator alloc]
        initWithStyle:(type == SYToastError) ? UIImpactFeedbackStyleHeavy : UIImpactFeedbackStyleLight];
    [haptic impactOccurred];
}

+ (UIColor *)bgColorForType:(SYToastType)type {
    switch (type) {
        case SYToastSuccess: return [SYTheme success];
        case SYToastError: return [SYTheme danger];
        case SYToastWarning: return [SYTheme warning];
        case SYToastInfo: return [SYTheme info];
    }
    return [SYTheme accent];
}

+ (UIImage *)iconForType:(SYToastType)type {
    switch (type) {
        case SYToastSuccess: return [SYTheme icon:@"checkmark.circle.fill" size:14 color:[UIColor whiteColor]];
        case SYToastError: return [SYTheme icon:@"xmark.circle.fill" size:14 color:[UIColor whiteColor]];
        case SYToastWarning: return [SYTheme icon:@"exclamationmark.triangle.fill" size:14 color:[UIColor whiteColor]];
        case SYToastInfo: return [SYTheme icon:@"info.circle.fill" size:14 color:[UIColor whiteColor]];
    }
    return nil;
}

@end
