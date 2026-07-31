#import "SYBaseHandler.h"
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface SYPatchHandler : SYBaseHandler

/// Patch records for session save.
@property (nonatomic, readonly) NSArray<NSDictionary *> *allPatches;

- (void)restoreAll;
- (BOOL)canUndo;
- (BOOL)canRedo;
- (void)undo;
- (void)redo;

@end

NS_ASSUME_NONNULL_END
