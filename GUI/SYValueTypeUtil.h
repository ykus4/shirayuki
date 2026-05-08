#pragma once

#import "ShirayukiMemory.hpp"
#import <Foundation/Foundation.h>

namespace SYValueTypeUtil {

/// NSString type tag ("int32", "float", etc.) → ValueType
inline Shirayuki::ValueType fromString(NSString *s) {
    if ([s isEqualToString:@"int16"])
        return Shirayuki::ValueType::Int16;
    if ([s isEqualToString:@"int64"])
        return Shirayuki::ValueType::Int64;
    if ([s isEqualToString:@"float"])
        return Shirayuki::ValueType::Float32;
    if ([s isEqualToString:@"double"])
        return Shirayuki::ValueType::Float64;
    if ([s isEqualToString:@"uint32"])
        return Shirayuki::ValueType::UInt32;
    return Shirayuki::ValueType::Int32; // default
}

/// ValueType → display short label
inline NSString *shortLabel(Shirayuki::ValueType type) {
    switch (type) {
        case Shirayuki::ValueType::Int8:
            return @"i8";
        case Shirayuki::ValueType::UInt8:
            return @"u8";
        case Shirayuki::ValueType::Int16:
            return @"i16";
        case Shirayuki::ValueType::UInt16:
            return @"u16";
        case Shirayuki::ValueType::Int32:
            return @"i32";
        case Shirayuki::ValueType::UInt32:
            return @"u32";
        case Shirayuki::ValueType::Int64:
            return @"i64";
        case Shirayuki::ValueType::UInt64:
            return @"u64";
        case Shirayuki::ValueType::Float32:
            return @"f32";
        case Shirayuki::ValueType::Float64:
            return @"f64";
    }
    return @"i32";
}

/// Parse NSString input into bytes for given type. Returns byte count written (0 on failure).
inline size_t parseValue(NSString *input, NSString *typeStr, uint8_t buf[8]) {
    memset(buf, 0, 8);
    if ([typeStr isEqualToString:@"float"]) {
        float v = [input floatValue];
        memcpy(buf, &v, 4);
        return 4;
    }
    if ([typeStr isEqualToString:@"double"]) {
        double v = [input doubleValue];
        memcpy(buf, &v, 8);
        return 8;
    }
    if ([typeStr isEqualToString:@"int64"]) {
        int64_t v = [input longLongValue];
        memcpy(buf, &v, 8);
        return 8;
    }
    if ([typeStr isEqualToString:@"int16"]) {
        int16_t v = (int16_t)[input intValue];
        memcpy(buf, &v, 2);
        return 2;
    }
    // Default: int32
    int32_t v = [input intValue];
    memcpy(buf, &v, 4);
    return 4;
}

/// Format a raw value buffer as display NSString for given type
inline NSString *formatValue(const uint8_t *buf, NSString *typeStr) {
    if ([typeStr isEqualToString:@"float"]) {
        float v;
        memcpy(&v, buf, 4);
        return [NSString stringWithFormat:@"%.3f", v];
    }
    if ([typeStr isEqualToString:@"double"]) {
        double v;
        memcpy(&v, buf, 8);
        return [NSString stringWithFormat:@"%.5f", v];
    }
    if ([typeStr isEqualToString:@"int64"]) {
        int64_t v;
        memcpy(&v, buf, 8);
        return [NSString stringWithFormat:@"%lld", v];
    }
    if ([typeStr isEqualToString:@"int16"]) {
        int16_t v;
        memcpy(&v, buf, 2);
        return [NSString stringWithFormat:@"%d", (int)v];
    }
    int32_t v;
    memcpy(&v, buf, 4);
    return [NSString stringWithFormat:@"%d (0x%X)", v, (uint32_t)v];
}

} // namespace SYValueTypeUtil
