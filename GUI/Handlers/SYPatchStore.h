#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// One tracked byte patch.
///
/// Replaces the untyped NSMutableDictionary with keys @"id"/@"address"/@"hex"/
/// @"original"/@"label"/@"applied", which appeared in ~40 places with no
/// compile-time checking of either key names or value types.
@interface SYPatchEntry : NSObject

/// Stable identity, so row deletion or reordering cannot misdirect an undo.
@property (nonatomic, readonly) NSUInteger patchId;
@property (nonatomic, readonly) uintptr_t address;
/// Bytes written by the patch, as canonical spaced hex ("90 90").
@property (nonatomic, readonly, copy) NSString *patchHex;
/// Bytes that were there before the patch was first applied.
@property (nonatomic, readonly, copy) NSString *originalHex;
@property (nonatomic, readonly, copy) NSString *label;
/// Whether the patch bytes are currently in memory.
@property (nonatomic, readonly) BOOL applied;

/// Label if set, otherwise the formatted address.
- (NSString *)displayName;

@end

/// Owns the patch list and its undo/redo history.
///
/// Split out of SYPatchHandler, which was 300 lines with over half of it
/// bookkeeping unrelated to being a tab handler. Every memory write is checked
/// here, and `applied` is only updated when the write actually succeeded — the
/// previous code set it unconditionally, so the UI reported a toggle that had
/// silently failed.
@interface SYPatchStore : NSObject

@property (nonatomic, readonly) NSArray<SYPatchEntry *> *entries;
@property (nonatomic, readonly) BOOL canUndo;
@property (nonatomic, readonly) BOOL canRedo;

/// Apply `hex` at `address` and track it. Returns the new entry, or nil with
/// `outError` set (malformed hex, unreadable original bytes, failed write).
- (nullable SYPatchEntry *)applyPatchAtAddress:(uintptr_t)address
                                           hex:(NSString *)hex
                                         label:(NSString *)label
                                         error:(NSString *_Nullable *_Nullable)outError;

/// Flip a patch between applied and restored. Returns NO with `outError` set if
/// the write failed; the entry's state is left as it really is.
- (BOOL)togglePatchAtIndex:(NSUInteger)index error:(NSString *_Nullable *_Nullable)outError;

/// Restore original bytes and stop tracking. Undoable.
- (BOOL)removePatchAtIndex:(NSUInteger)index error:(NSString *_Nullable *_Nullable)outError;

/// Restore every applied patch. Returns the number restored; `outFailed` gets the
/// number that could not be written.
- (NSUInteger)restoreAllWithFailureCount:(nullable NSUInteger *)outFailed;

/// Undo/redo one step. Both return a user-facing description of what happened,
/// or nil when there was nothing to do — the previous implementation announced
/// "Undone" even when every branch had been skipped because the target entry no
/// longer existed.
- (nullable NSString *)undoWithError:(NSString *_Nullable *_Nullable)outError;
- (nullable NSString *)redoWithError:(NSString *_Nullable *_Nullable)outError;

/// Session serialisation: one dictionary per entry.
- (NSArray<NSDictionary *> *)serializedEntries;

@end

NS_ASSUME_NONNULL_END
