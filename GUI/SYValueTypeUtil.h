#pragma once

#import "ShirayukiMemory.hpp"
#import <Foundation/Foundation.h>

/// Thin ObjC++ adapters over Shirayuki::ValueFormat.
///
/// All type-dependent behaviour lives in the C++ descriptor table
/// (ShirayukiMemory/ValueType.cpp). Nothing here may reimplement sizes, tag
/// lookups or per-type formatting — that duplication is what let the ObjC and
/// C++ halves of the app disagree about what a type tag means.
namespace SYValueTypeUtil {

/// NSString type tag → ValueType. Accepts canonical tags ("int32", "float") and
/// short labels ("i32", "f32"). Unknown tags fall back to Int32.
inline Shirayuki::ValueType fromString(NSString *s) {
    return Shirayuki::ValueFormat::fromTag(s ? [s UTF8String] : "int32");
}

/// NSString type tag → ValueType, reporting whether the tag was recognised.
/// Prefer this on user input paths so a typo is surfaced rather than silently
/// scanned as int32.
inline BOOL tryFromString(NSString *s, Shirayuki::ValueType &out) {
    if (!s)
        return NO;
    return Shirayuki::ValueFormat::tryFromTag([s UTF8String], out) ? YES : NO;
}

/// ValueType → compact UI label ("i32").
inline NSString *shortLabel(Shirayuki::ValueType type) {
    return @(Shirayuki::valueTypeLabel(type).c_str());
}

/// ValueType → canonical, persistable tag ("int32").
inline NSString *canonicalTag(Shirayuki::ValueType type) {
    return @(Shirayuki::ValueFormat::toTag(type).c_str());
}

/// Byte width of a type tag.
inline size_t sizeOfTag(NSString *typeStr) {
    return Shirayuki::valueTypeSize(fromString(typeStr));
}

/// Parse user input into bytes. Returns bytes written, 0 on failure; `buf` is
/// zeroed either way. Never throws.
inline size_t parseValue(NSString *input, NSString *typeStr,
                         uint8_t buf[Shirayuki::kMaxValueSize]) {
    return Shirayuki::ValueFormat::parse(input ? [input UTF8String] : "", fromString(typeStr), buf);
}

/// Format bytes as a plain, re-parseable value string. Use for edit fields and
/// anything persisted (session files, JSON export).
inline NSString *formatValue(const uint8_t *buf, NSString *typeStr) {
    return @(Shirayuki::ValueFormat::format(buf, fromString(typeStr)).c_str());
}

/// Format bytes for display in a cell: integers gain a hex annotation, floats
/// use fixed notation. Not re-parseable.
inline NSString *displayValue(const uint8_t *buf, NSString *typeStr) {
    return @(Shirayuki::ValueFormat::formatDisplay(buf, fromString(typeStr)).c_str());
}

/// Canonical tags of every supported type, in enumerator order. Single source
/// for the type-cycling button, so adding a ValueType needs no UI change.
inline NSArray<NSString *> *allTags() {
    auto tags = Shirayuki::ValueFormat::allTags();
    NSMutableArray<NSString *> *out = [NSMutableArray arrayWithCapacity:tags.size()];
    for (const auto &t : tags)
        [out addObject:@(t.c_str())];
    return out;
}

} // namespace SYValueTypeUtil
