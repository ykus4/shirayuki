#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYPatchHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
@property (nonatomic, readonly) NSArray<NSDictionary *> *allPatches;
- (void)restoreAll;
- (BOOL)canUndo;
- (BOOL)canRedo;
- (void)undo;
- (void)redo;
@end
