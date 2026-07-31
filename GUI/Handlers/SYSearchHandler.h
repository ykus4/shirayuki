#import "SYBaseHandler.h"
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Narrowing filters offered after a scan. Previously passed around as bare
/// strings ("changed", "increased", ...) that had to match across three separate
/// places, with an unrecognised value silently emptying the result list.
typedef NS_ENUM(NSInteger, SYNarrowFilter) {
    SYNarrowChanged,
    SYNarrowUnchanged,
    SYNarrowIncreased,
    SYNarrowDecreased,
    SYNarrowGreaterThan,
    SYNarrowLessThan,
    SYNarrowExact,
};

@interface SYSearchHandler : SYBaseHandler

/// Canonical type tag ("int32", "float", ...) or a non-numeric search mode
/// ("hex", "string", "regex").
@property (nonatomic, copy) NSString *searchType;
@property (nonatomic, readonly) BOOL hasResults;
/// YES once a scan has produced narrowable (typed) results.
@property (nonatomic, readonly) BOOL isNarrowing;

- (NSString *)shortType;
- (void)cycleType;

/// Apply a filter. `input` is required by Exact, GreaterThan and LessThan, and
/// ignored otherwise.
- (void)narrowWithFilter:(SYNarrowFilter)filter input:(nullable NSString *)input;

- (void)batchModify:(NSString *)value;
- (void)resetSearch;
- (NSArray<NSString *> *)searchHistory;

/// Writes results to a JSON file. Returns the path, or nil on failure —
/// including a failed file write, which was previously reported as success.
- (nullable NSString *)exportResultsAsJSON;

@end

NS_ASSUME_NONNULL_END
