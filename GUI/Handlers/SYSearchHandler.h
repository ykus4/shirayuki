#import "SYTabHandler.h"
#import <UIKit/UIKit.h>

@class ShirayukiViewController;

@interface SYSearchHandler : NSObject <SYTabHandler>
@property (nonatomic, weak) ShirayukiViewController *viewController;
@property (nonatomic, strong) NSString *searchType; // "int32","float","hex","string"
@property (nonatomic, assign) BOOL hasResults;
@property (nonatomic, assign) BOOL isNarrowing;

- (NSString *)shortType;
- (void)cycleType;
- (void)narrow:(NSString *)mode; // "changed","unchanged","increased","decreased","exact"
- (void)batchModify:(NSString *)value;
- (void)resetSearch;
- (NSArray<NSString *> *)searchHistory;
@end
