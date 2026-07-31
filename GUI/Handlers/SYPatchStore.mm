#import "SYPatchStore.h"

#import "SYInput.h"
#import "ShirayukiMemory.hpp"

using namespace Shirayuki;

#pragma mark - Entry

@interface SYPatchEntry ()
@property (nonatomic, assign) NSUInteger patchId;
@property (nonatomic, assign) uintptr_t address;
@property (nonatomic, copy) NSString *patchHex;
@property (nonatomic, copy) NSString *originalHex;
@property (nonatomic, copy) NSString *label;
@property (nonatomic, assign) BOOL applied;
@end

@implementation SYPatchEntry

- (NSString *)displayName {
    return self.label.length ? self.label : SYFormatAddress(self.address);
}

@end

#pragma mark - Undo records

/// What an undo step should do. Replaces the @"remove"/@"restore"/@"reapply"/
/// @"readd" strings that were compared with isEqualToString: in six places.
typedef NS_ENUM(NSInteger, SYPatchOp) {
    /// The patch was added; undoing it removes the entry.
    SYPatchOpAdd,
    /// The patch's applied state was flipped; undoing it flips back.
    SYPatchOpToggle,
    /// The patch was deleted; undoing it re-inserts the entry.
    SYPatchOpDelete,
};

@interface SYPatchRecord : NSObject
@property (nonatomic, assign) SYPatchOp op;
@property (nonatomic, assign) NSUInteger patchId;
/// Retained for SYPatchOpDelete so the entry can be brought back.
@property (nonatomic, strong, nullable) SYPatchEntry *entry;
/// Position to restore a deleted entry to.
@property (nonatomic, assign) NSUInteger index;
@end

@implementation SYPatchRecord
@end

#pragma mark - Store

@interface SYPatchStore ()
@property (nonatomic, strong) NSMutableArray<SYPatchEntry *> *mutableEntries;
@property (nonatomic, strong) NSMutableArray<SYPatchRecord *> *undoStack;
@property (nonatomic, strong) NSMutableArray<SYPatchRecord *> *redoStack;
@property (nonatomic, assign) NSUInteger nextPatchId;
@end

@implementation SYPatchStore

- (instancetype)init {
    self = [super init];
    if (self) {
        _mutableEntries = [NSMutableArray new];
        _undoStack = [NSMutableArray new];
        _redoStack = [NSMutableArray new];
        _nextPatchId = 1;
    }
    return self;
}

- (NSArray<SYPatchEntry *> *)entries {
    return [_mutableEntries copy];
}

- (BOOL)canUndo {
    return _undoStack.count > 0;
}

- (BOOL)canRedo {
    return _redoStack.count > 0;
}

#pragma mark - Memory helpers

/// Write spaced hex to an address. Every caller used to ignore both the empty-
/// bytes case and the write status.
- (BOOL)writeHex:(NSString *)hex toAddress:(uintptr_t)address error:(NSString **)outError {
    const std::vector<uint8_t> bytes = Hex::toBytes([hex UTF8String]);
    if (bytes.empty()) {
        if (outError)
            *outError = @"No valid bytes to write";
        return NO;
    }
    if (Memory::write(address, bytes.data(), bytes.size()) != Status::Success) {
        if (outError)
            *outError = [NSString stringWithFormat:@"Write to %@ failed", SYFormatAddress(address)];
        return NO;
    }
    return YES;
}

- (SYPatchEntry *)entryWithId:(NSUInteger)patchId index:(NSUInteger *)outIndex {
    for (NSUInteger i = 0; i < _mutableEntries.count; i++) {
        if (_mutableEntries[i].patchId == patchId) {
            if (outIndex)
                *outIndex = i;
            return _mutableEntries[i];
        }
    }
    return nil;
}

- (void)pushUndo:(SYPatchRecord *)record clearRedo:(BOOL)clearRedo {
    [_undoStack addObject:record];
    if (clearRedo)
        [_redoStack removeAllObjects];
}

#pragma mark - Mutations

- (SYPatchEntry *)applyPatchAtAddress:(uintptr_t)address
                                  hex:(NSString *)hex
                                label:(NSString *)label
                                error:(NSString **)outError {
    if (!Hex::isValid([hex UTF8String])) {
        if (outError)
            *outError = @"Invalid hex bytes";
        return nil;
    }

    // Patch::createWithHex snapshots the original bytes, so it must succeed
    // before anything is written.
    Patch patch = Patch::createWithHex(address, [hex UTF8String]);
    if (!patch.isValid()) {
        if (outError)
            *outError = [NSString stringWithFormat:@"Cannot read %@", SYFormatAddress(address)];
        return nil;
    }
    if (!patch.apply()) {
        if (outError)
            *outError = [NSString stringWithFormat:@"Write to %@ failed", SYFormatAddress(address)];
        return nil;
    }

    SYPatchEntry *entry = [[SYPatchEntry alloc] init];
    entry.patchId = _nextPatchId++;
    entry.address = address;
    // Canonical spacing from the core, so the stored hex round-trips.
    entry.patchHex = @(patch.patchHex().c_str());
    entry.originalHex = @(patch.originalHex().c_str());
    entry.label = label ?: @"";
    entry.applied = YES;

    [_mutableEntries addObject:entry];

    SYPatchRecord *record = [[SYPatchRecord alloc] init];
    record.op = SYPatchOpAdd;
    record.patchId = entry.patchId;
    [self pushUndo:record clearRedo:YES];

    return entry;
}

/// Flip `entry` and report whether the memory write actually happened.
- (BOOL)setEntry:(SYPatchEntry *)entry applied:(BOOL)applied error:(NSString **)outError {
    NSString *hex = applied ? entry.patchHex : entry.originalHex;
    if (![self writeHex:hex toAddress:entry.address error:outError])
        return NO;
    entry.applied = applied;
    return YES;
}

- (BOOL)togglePatchAtIndex:(NSUInteger)index error:(NSString **)outError {
    if (index >= _mutableEntries.count) {
        if (outError)
            *outError = @"No such patch";
        return NO;
    }

    SYPatchEntry *entry = _mutableEntries[index];
    if (![self setEntry:entry applied:!entry.applied error:outError])
        return NO;

    SYPatchRecord *record = [[SYPatchRecord alloc] init];
    record.op = SYPatchOpToggle;
    record.patchId = entry.patchId;
    [self pushUndo:record clearRedo:YES];
    return YES;
}

- (BOOL)removePatchAtIndex:(NSUInteger)index error:(NSString **)outError {
    if (index >= _mutableEntries.count) {
        if (outError)
            *outError = @"No such patch";
        return NO;
    }

    SYPatchEntry *entry = _mutableEntries[index];

    // Put the original bytes back before losing track of them.
    if (entry.applied && ![self setEntry:entry applied:NO error:outError])
        return NO;

    // Deletion is undoable, and the entry is retained by the record. Previously
    // deleting a patch left undo items pointing at an entry that no longer
    // existed, and those undos then silently did nothing.
    SYPatchRecord *record = [[SYPatchRecord alloc] init];
    record.op = SYPatchOpDelete;
    record.patchId = entry.patchId;
    record.entry = entry;
    record.index = index;
    [self pushUndo:record clearRedo:YES];

    [_mutableEntries removeObjectAtIndex:index];
    return YES;
}

- (NSUInteger)restoreAllWithFailureCount:(NSUInteger *)outFailed {
    NSUInteger restored = 0, failed = 0;
    for (SYPatchEntry *entry in _mutableEntries) {
        if (!entry.applied)
            continue;
        NSString *error = nil;
        if ([self setEntry:entry applied:NO error:&error])
            restored++;
        else
            failed++;
    }
    if (outFailed)
        *outFailed = failed;
    return restored;
}

#pragma mark - Undo / redo

- (NSString *)undoWithError:(NSString **)outError {
    if (!_undoStack.count)
        return nil;

    SYPatchRecord *record = _undoStack.lastObject;

    switch (record.op) {
        case SYPatchOpAdd: {
            NSUInteger index = 0;
            SYPatchEntry *entry = [self entryWithId:record.patchId index:&index];
            if (!entry) {
                // The entry is gone (deleted separately). Drop the stale record
                // and say so, rather than reporting a successful undo.
                [_undoStack removeLastObject];
                if (outError)
                    *outError = @"Patch no longer exists";
                return nil;
            }
            if (entry.applied && ![self setEntry:entry applied:NO error:outError])
                return nil;

            SYPatchRecord *redo = [[SYPatchRecord alloc] init];
            redo.op = SYPatchOpDelete; // redoing an add = undoing a delete
            redo.patchId = entry.patchId;
            redo.entry = entry;
            redo.index = index;
            [_redoStack addObject:redo];

            [_undoStack removeLastObject];
            [_mutableEntries removeObjectAtIndex:index];
            return @"Undid patch";
        }

        case SYPatchOpToggle: {
            SYPatchEntry *entry = [self entryWithId:record.patchId index:NULL];
            if (!entry) {
                [_undoStack removeLastObject];
                if (outError)
                    *outError = @"Patch no longer exists";
                return nil;
            }
            if (![self setEntry:entry applied:!entry.applied error:outError])
                return nil;

            SYPatchRecord *redo = [[SYPatchRecord alloc] init];
            redo.op = SYPatchOpToggle;
            redo.patchId = entry.patchId;
            [_redoStack addObject:redo];

            [_undoStack removeLastObject];
            return entry.applied ? @"Re-applied" : @"Restored";
        }

        case SYPatchOpDelete: {
            SYPatchEntry *entry = record.entry;
            if (!entry) {
                [_undoStack removeLastObject];
                if (outError)
                    *outError = @"Patch no longer exists";
                return nil;
            }
            // Bring it back applied, matching how it was created.
            if (![self setEntry:entry applied:YES error:outError])
                return nil;

            const NSUInteger index = MIN(record.index, _mutableEntries.count);
            [_mutableEntries insertObject:entry atIndex:index];

            SYPatchRecord *redo = [[SYPatchRecord alloc] init];
            redo.op = SYPatchOpAdd; // redoing a delete = undoing an add
            redo.patchId = entry.patchId;
            [_redoStack addObject:redo];

            [_undoStack removeLastObject];
            return @"Restored patch";
        }
    }
    return nil;
}

- (NSString *)redoWithError:(NSString **)outError {
    if (!_redoStack.count)
        return nil;

    // The record is only popped once the operation has succeeded. The previous
    // implementation popped first and then returned early on a missing entry,
    // losing the redo item entirely.
    SYPatchRecord *record = _redoStack.lastObject;

    switch (record.op) {
        case SYPatchOpAdd: {
            NSUInteger index = 0;
            SYPatchEntry *entry = [self entryWithId:record.patchId index:&index];
            if (!entry) {
                [_redoStack removeLastObject];
                if (outError)
                    *outError = @"Patch no longer exists";
                return nil;
            }
            if (entry.applied && ![self setEntry:entry applied:NO error:outError])
                return nil;

            SYPatchRecord *undo = [[SYPatchRecord alloc] init];
            undo.op = SYPatchOpDelete;
            undo.patchId = entry.patchId;
            undo.entry = entry;
            undo.index = index;
            [_undoStack addObject:undo];

            [_redoStack removeLastObject];
            [_mutableEntries removeObjectAtIndex:index];
            return @"Removed patch";
        }

        case SYPatchOpToggle: {
            SYPatchEntry *entry = [self entryWithId:record.patchId index:NULL];
            if (!entry) {
                [_redoStack removeLastObject];
                if (outError)
                    *outError = @"Patch no longer exists";
                return nil;
            }
            if (![self setEntry:entry applied:!entry.applied error:outError])
                return nil;

            SYPatchRecord *undo = [[SYPatchRecord alloc] init];
            undo.op = SYPatchOpToggle;
            undo.patchId = entry.patchId;
            [_undoStack addObject:undo];

            [_redoStack removeLastObject];
            return entry.applied ? @"Re-applied" : @"Restored";
        }

        case SYPatchOpDelete: {
            SYPatchEntry *entry = record.entry;
            if (!entry) {
                [_redoStack removeLastObject];
                if (outError)
                    *outError = @"Patch no longer exists";
                return nil;
            }
            if (![self setEntry:entry applied:YES error:outError])
                return nil;

            const NSUInteger index = MIN(record.index, _mutableEntries.count);
            [_mutableEntries insertObject:entry atIndex:index];

            SYPatchRecord *undo = [[SYPatchRecord alloc] init];
            undo.op = SYPatchOpAdd;
            undo.patchId = entry.patchId;
            [_undoStack addObject:undo];

            [_redoStack removeLastObject];
            return @"Re-added patch";
        }
    }
    return nil;
}

#pragma mark - Serialisation

- (NSArray<NSDictionary *> *)serializedEntries {
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:_mutableEntries.count];
    for (SYPatchEntry *entry in _mutableEntries) {
        [out addObject:@{
            @"address" : @((unsigned long long)entry.address),
            @"hex" : entry.patchHex,
            @"original" : entry.originalHex,
            @"label" : entry.label,
            @"applied" : @(entry.applied),
        }];
    }
    return out;
}

@end
