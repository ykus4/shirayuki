#pragma once

#import <Foundation/Foundation.h>

/// Shared input parsing and address formatting for the tab handlers.
///
/// Every handler used to open `performAction:` with its own copy of
///
///     NSArray *parts = [input componentsSeparatedByString:@" "];
///     unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
///     if (!addr) { toast(@"Invalid address"); return; }
///
/// which has three bugs in every copy: a NULL end pointer means parse failure is
/// never detected, so "0x1zzz" silently truncates to 0x1; `!addr` conflates
/// address zero with failure; and empty components from repeated spaces are kept
/// as arguments. Use SYCommand instead.

NS_ASSUME_NONNULL_BEGIN

// The free functions below are defined in SYInput.m, which is compiled as plain
// Objective-C. Without this, an ObjC++ caller mangles the call as
// `SYFormatAddress(unsigned long)` while the definition exports the C symbol
// `_SYFormatAddress`, and the link fails. `-fsyntax-only` does not catch it.
#ifdef __cplusplus
extern "C" {
#endif

/// Canonical address rendering. Previously spelled three different ways
/// (`0x%lX`, `0x%llX`, `0x%lX` with an explicit cast) across ten call sites.
NSString *SYFormatAddress(uintptr_t address);

/// Parse a standalone hexadecimal address. Returns NO on any malformed input,
/// including the empty string and values with trailing characters.
BOOL SYParseAddress(NSString *_Nullable text, uintptr_t *outAddress);

#ifdef __cplusplus
} // extern "C"
#endif

/// A parsed "<address> [arg...]" handler command line.
@interface SYCommand : NSObject

/// Parse `input`. The address is hexadecimal, with or without a `0x` prefix, and
/// must consume its whole token — trailing garbage is an error rather than a
/// silent truncation. `minArgs` is the number of arguments required *after* the
/// address. Never returns nil; check `ok`.
+ (instancetype)parse:(nullable NSString *)input minArgs:(NSUInteger)minArgs;

/// NO when the address could not be parsed or a required argument is absent.
@property (nonatomic, readonly) BOOL ok;
@property (nonatomic, readonly) uintptr_t address;
/// Arguments after the address, with empty components removed. Never nil.
@property (nonatomic, readonly) NSArray<NSString *> *args;
/// User-facing reason, non-nil only when `ok` is NO.
@property (nonatomic, readonly, nullable) NSString *error;

/// Argument at `index`, or nil when absent. Keeps handlers from indexing past
/// the end of a short argument list.
- (nullable NSString *)argAt:(NSUInteger)index;

/// Argument at `index`, or `fallback` when absent or empty.
- (NSString *)argAt:(NSUInteger)index or:(NSString *)fallback;

/// Argument at `index` parsed as a base-10 integer, or `fallback` when absent or
/// malformed. Clamped to [min, max] so a hostile length cannot become a huge
/// allocation or a negative count.
- (NSInteger)integerArgAt:(NSUInteger)index
                 fallback:(NSInteger)fallback
                      min:(NSInteger)minValue
                      max:(NSInteger)maxValue;

@end

NS_ASSUME_NONNULL_END
