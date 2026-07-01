#pragma once
#import <UIKit/UIKit.h>

// SYHotkey — gesture-based hotkey framework skeleton.
//
// Not wired up yet. The intent is:
//   1. `+ installGlobalRecognizersOn:` attaches high-priority gesture recognizers
//      to the target window so multi-finger taps work even while the app UI is
//      handling touches.
//   2. Consumers register (gesture, handler) pairs via `+ bind:action:`. The
//      recognizer callback dispatches to the block on the main queue.
//
// Real integration requires deciding on hit-test behavior (should Shirayuki
// swallow multi-finger taps, or let them fall through?). Left as TODO for
// on-device verification.
typedef void (^SYHotkeyBlock)(void);

typedef NS_ENUM(NSInteger, SYHotkeyKind) {
    SYHotkeyThreeFingerTap = 0,
    SYHotkeyFourFingerTap,
    SYHotkeyLongPressTwoFinger,
};

@interface SYHotkey : NSObject

/// Attach recognizers to `window`. Safe to call multiple times — subsequent
/// calls are no-ops on the same window.
+ (void)installGlobalRecognizersOn:(UIWindow *)window;

/// Bind a block to a hotkey kind. Overwrites any prior binding for that kind.
+ (void)bind:(SYHotkeyKind)kind action:(SYHotkeyBlock)action;

/// Unbind a single kind.
+ (void)unbind:(SYHotkeyKind)kind;

@end
