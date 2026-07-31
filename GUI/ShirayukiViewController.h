#import <UIKit/UIKit.h>

@interface ShirayukiViewController : UIViewController
- (void)reloadTable;
- (void)showModifyAlertForAddress:(uintptr_t)addr type:(NSString *)type;
- (void)showBatchModifyAlert;
/// Present a confirmation prompt before an irreversible memory write.
- (void)confirmAction:(NSString *)title
              message:(NSString *)message
          confirmVerb:(NSString *)verb
             onAccept:(void (^)(void))onAccept;
@end
