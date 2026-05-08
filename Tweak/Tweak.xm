/**
 * Shirayuki — iOS Memory Tweak
 *
 * Injects a floating GUI into the target app for interactive
 * memory searching, patching, freezing, and pointer scanning.
 */

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "ShirayukiMemory.hpp"
#import "Freeze.hpp"
#import "ShirayukiWindow.h"

using namespace Shirayuki;

// --- Floating toggle button ---
static UIButton *g_toggleButton = nil;

static void createToggleButton() {
    g_toggleButton = [UIButton buttonWithType:UIButtonTypeCustom];
    g_toggleButton.frame = CGRectMake(10, 100, 44, 44);
    g_toggleButton.backgroundColor = [UIColor colorWithRed:0 green:0.8 blue:0.9 alpha:0.85];
    g_toggleButton.layer.cornerRadius = 22;
    g_toggleButton.layer.shadowColor = [UIColor blackColor].CGColor;
    g_toggleButton.layer.shadowOffset = CGSizeMake(0, 2);
    g_toggleButton.layer.shadowOpacity = 0.5;
    g_toggleButton.layer.shadowRadius = 4;
    [g_toggleButton setTitle:@"S" forState:UIControlStateNormal];
    [g_toggleButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    g_toggleButton.titleLabel.font = [UIFont boldSystemFontOfSize:18];

    [g_toggleButton addTarget:[ShirayukiWindow shared]
                       action:@selector(toggle)
             forControlEvents:UIControlEventTouchUpInside];

    // Make the button draggable
    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc]
        initWithTarget:g_toggleButton action:@selector(handleButtonDrag:)];
    [g_toggleButton addGestureRecognizer:pan];
}

// Category for drag on the toggle button
@interface UIButton (ShirayukiDrag)
- (void)handleButtonDrag:(UIPanGestureRecognizer *)gesture;
@end

@implementation UIButton (ShirayukiDrag)
- (void)handleButtonDrag:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self.superview];
    self.center = CGPointMake(self.center.x + translation.x,
                              self.center.y + translation.y);
    [gesture setTranslation:CGPointZero inView:self.superview];
}
@end

// --- Hook into the app's main window to add our button ---
%hook UIWindow

- (void)makeKeyAndVisible {
    %orig;

    // Only add to the app's main window, not our own
    if (self != [ShirayukiWindow shared] && !g_toggleButton.superview) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC),
                       dispatch_get_main_queue(), ^{
            if (!g_toggleButton) {
                createToggleButton();
            }
            [self addSubview:g_toggleButton];
            NSLog(@"[Shirayuki] Toggle button added to window");
        });
    }
}

%end

// --- Entry point ---
%ctor {
    @autoreleasepool {
        NSLog(@"[Shirayuki] Loaded");

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC),
                       dispatch_get_main_queue(), ^{
            // Pre-initialize the GUI window
            [ShirayukiWindow shared];
            NSLog(@"[Shirayuki] GUI ready — tap the floating button to open");
        });
    }
}
