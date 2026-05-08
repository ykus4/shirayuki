#import "ShirayukiWindow.h"
#import "ShirayukiViewController.h"

@implementation ShirayukiWindow

+ (instancetype)shared {
    static ShirayukiWindow *instance;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        CGRect frame = CGRectMake(20, 80, 340, 500);
        instance = [[ShirayukiWindow alloc] initWithFrame:frame];
        instance.windowLevel = UIWindowLevelAlert + 100;
        instance.backgroundColor = [UIColor clearColor];
        instance.rootViewController = [[ShirayukiViewController alloc] init];
        instance.layer.cornerRadius = 12;
        instance.clipsToBounds = YES;
        instance.hidden = YES;
    });
    return instance;
}

- (void)show {
    self.hidden = NO;
    [self makeKeyAndVisible];
}

- (void)hide {
    self.hidden = YES;
}

- (void)toggle {
    if (self.hidden) [self show];
    else [self hide];
}

@end
