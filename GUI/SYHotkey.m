#import "SYHotkey.h"

@implementation SYHotkey

// Skeleton: real device-side wiring is deferred until we can validate gesture
// hit-testing against a running app. The API is stable so callers can start
// invoking `+bind:action:` today; the recognizer install path is a no-op stub.

static NSMutableDictionary<NSNumber *, SYHotkeyBlock> *g_bindings = nil;

+ (void)initialize {
    if (self == [SYHotkey class]) {
        g_bindings = [NSMutableDictionary new];
    }
}

+ (void)installGlobalRecognizersOn:(UIWindow *)window {
    // TODO(device): attach UITapGestureRecognizer with numberOfTouchesRequired = 3/4
    // and UILongPressGestureRecognizer for two-finger long press. Requires setting
    // cancelsTouchesInView based on whether the app already handles multi-touch.
    (void)window;
}

+ (void)bind:(SYHotkeyKind)kind action:(SYHotkeyBlock)action {
    if (action)
        g_bindings[@(kind)] = [action copy];
}

+ (void)unbind:(SYHotkeyKind)kind {
    [g_bindings removeObjectForKey:@(kind)];
}

@end
