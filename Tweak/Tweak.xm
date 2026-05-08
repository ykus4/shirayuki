/**
 * Shirayuki — iOS Memory Tweak
 *
 * Injects a floating GUI into the target app for interactive
 * memory searching, patching, freezing, watching, and pointer scanning.
 */

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "ShirayukiMemory.hpp"
#import "Freeze.hpp"
#import "ShirayukiWindow.h"
#import "SYDragButton.h"

using namespace Shirayuki;

static SYDragButton *g_toggleButton = nil;

static void createToggleButton() {
    g_toggleButton = [[SYDragButton alloc] initWithFrame:CGRectMake(10, 100, 46, 46)];
    g_toggleButton.onTap = ^{
        [[ShirayukiWindow shared] toggle];
    };
}

%hook UIWindow

- (void)makeKeyAndVisible {
    %orig;

    if (self != [ShirayukiWindow shared] && !g_toggleButton.superview) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            if (!g_toggleButton) createToggleButton();
            [self addSubview:g_toggleButton];
            NSLog(@"[Shirayuki] Ready");
        });
    }
}

%end

%ctor {
    @autoreleasepool {
        NSLog(@"[Shirayuki] Loaded");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            [ShirayukiWindow shared];
        });
    }
}
