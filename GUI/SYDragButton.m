#import "SYDragButton.h"
#import "SYTheme.h"

@implementation SYDragButton {
    CGPoint _startCenter;
    BOOL _dragging;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupStyle];
        [self setupGesture];
    }
    return self;
}

- (void)setupStyle {
    self.backgroundColor = [SYTheme accent];
    self.layer.cornerRadius = self.bounds.size.width / 2.0;
    self.layer.shadowColor = [UIColor blackColor].CGColor;
    self.layer.shadowOffset = CGSizeMake(0, 3);
    self.layer.shadowOpacity = 0.6;
    self.layer.shadowRadius = 6;

    // Snowflake icon
    UIImage *icon = [SYTheme icon:@"snowflake" size:20 color:[UIColor whiteColor]];
    [self setImage:icon forState:UIControlStateNormal];
    self.tintColor = [UIColor whiteColor];
}

- (void)setupGesture {
    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc]
        initWithTarget:self action:@selector(handlePan:)];
    [self addGestureRecognizer:pan];

    [self addTarget:self action:@selector(handleTap) forControlEvents:UIControlEventTouchUpInside];
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    switch (gesture.state) {
        case UIGestureRecognizerStateBegan:
            _startCenter = self.center;
            _dragging = NO;
            // Scale up slightly
            [UIView animateWithDuration:0.15 animations:^{
                self.transform = CGAffineTransformMakeScale(1.1, 1.1);
            }];
            break;

        case UIGestureRecognizerStateChanged: {
            CGPoint translation = [gesture translationInView:self.superview];
            self.center = CGPointMake(_startCenter.x + translation.x,
                                      _startCenter.y + translation.y);
            _dragging = YES;
            break;
        }

        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateCancelled: {
            [UIView animateWithDuration:0.15 animations:^{
                self.transform = CGAffineTransformIdentity;
            }];

            // Snap to edge
            [self snapToEdge];
            break;
        }

        default: break;
    }
}

- (void)snapToEdge {
    CGRect bounds = self.superview.bounds;
    CGFloat margin = 8;
    CGFloat midX = bounds.size.width / 2.0;

    CGPoint target = self.center;
    if (self.center.x < midX) {
        target.x = self.bounds.size.width / 2.0 + margin;
    } else {
        target.x = bounds.size.width - self.bounds.size.width / 2.0 - margin;
    }

    // Clamp Y
    CGFloat halfH = self.bounds.size.height / 2.0;
    target.y = MAX(halfH + margin, MIN(target.y, bounds.size.height - halfH - margin));

    [UIView animateWithDuration:0.3 delay:0 usingSpringWithDamping:0.7
          initialSpringVelocity:0 options:0 animations:^{
        self.center = target;
    } completion:nil];
}

- (void)handleTap {
    if (_dragging) {
        _dragging = NO;
        return;
    }

    // Haptic
    UIImpactFeedbackGenerator *haptic = [[UIImpactFeedbackGenerator alloc]
        initWithStyle:UIImpactFeedbackStyleMedium];
    [haptic impactOccurred];

    // Scale animation
    [UIView animateWithDuration:0.1 animations:^{
        self.transform = CGAffineTransformMakeScale(0.85, 0.85);
    } completion:^(BOOL finished) {
        [UIView animateWithDuration:0.1 animations:^{
            self.transform = CGAffineTransformIdentity;
        }];
    }];

    if (self.onTap) self.onTap();
}

@end
