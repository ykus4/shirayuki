#pragma once
#import <Foundation/Foundation.h>

// Shorthands for the repeated pattern `dispatch_async(global) → dispatch_async(main)`
// used across all tab handlers. Keeping this as a header-only inline helper avoids
// a new translation unit and keeps callsites terse.

/// Run `work` on the user-initiated global queue, then `completion` on the main queue.
NS_INLINE void SYAsync(void (^work)(void), void (^completion)(void)) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (work)
            work();
        if (completion)
            dispatch_async(dispatch_get_main_queue(), completion);
    });
}

/// Run a block on the main queue, dispatching asynchronously if not already there.
NS_INLINE void SYOnMain(void (^block)(void)) {
    if (!block)
        return;
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}
