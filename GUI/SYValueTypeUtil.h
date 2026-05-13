#pragma once

#import "ShirayukiMemory.hpp"
#import <Foundation/Foundation.h>

namespace SYValueTypeUtil {

/// NSString type tag ("int32", "float", etc.) → ValueType
inline Shirayuki::ValueType fromString(NSString *s) {
    return Shirayuki::ValueFormat::fromTag(s ? [s UTF8String] : "int32");
}

/// ValueType → display short label
inline NSString *shortLabel(Shirayuki::ValueType type) {
    return @(Shirayuki::valueTypeLabel(type).c_str());
}

/// Parse NSString input into bytes for given type. Returns byte count written (0 on failure).
inline size_t parseValue(NSString *input, NSString *typeStr, uint8_t buf[8]) {
    auto vt = Shirayuki::ValueFormat::fromTag(typeStr ? [typeStr UTF8String] : "int32");
    return Shirayuki::ValueFormat::parse(input ? [input UTF8String] : "", vt, buf);
}

/// Format a raw value buffer as display NSString for given type
inline NSString *formatValue(const uint8_t *buf, NSString *typeStr) {
    auto vt = Shirayuki::ValueFormat::fromTag(typeStr ? [typeStr UTF8String] : "int32");
    return @(Shirayuki::ValueFormat::format(buf, vt).c_str());
}

} // namespace SYValueTypeUtil
