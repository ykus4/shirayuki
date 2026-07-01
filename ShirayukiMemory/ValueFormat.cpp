#include "ShirayukiMemory.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace Shirayuki {

size_t valueTypeSize(ValueType type) {
    switch (type) {
        case ValueType::Int8:
        case ValueType::UInt8:
            return 1;
        case ValueType::Int16:
        case ValueType::UInt16:
            return 2;
        case ValueType::Int32:
        case ValueType::UInt32:
        case ValueType::Float32:
            return 4;
        case ValueType::Int64:
        case ValueType::UInt64:
        case ValueType::Float64:
            return 8;
    }
    return 4;
}

std::string valueTypeLabel(ValueType type) {
    switch (type) {
        case ValueType::Int8:
            return "i8";
        case ValueType::UInt8:
            return "u8";
        case ValueType::Int16:
            return "i16";
        case ValueType::UInt16:
            return "u16";
        case ValueType::Int32:
            return "i32";
        case ValueType::UInt32:
            return "u32";
        case ValueType::Int64:
            return "i64";
        case ValueType::UInt64:
            return "u64";
        case ValueType::Float32:
            return "f32";
        case ValueType::Float64:
            return "f64";
    }
    return "?";
}

// Dispatch a callable at (buf, ValueType) to the concrete T for that type.
// Centralizes the 10-case switch so each caller doesn't repeat it.
template <typename F> auto dispatchByType(ValueType type, F &&f) {
    switch (type) {
        case ValueType::Int8:
            return f((int8_t *)nullptr);
        case ValueType::UInt8:
            return f((uint8_t *)nullptr);
        case ValueType::Int16:
            return f((int16_t *)nullptr);
        case ValueType::UInt16:
            return f((uint16_t *)nullptr);
        case ValueType::Int32:
            return f((int32_t *)nullptr);
        case ValueType::UInt32:
            return f((uint32_t *)nullptr);
        case ValueType::Int64:
            return f((int64_t *)nullptr);
        case ValueType::UInt64:
            return f((uint64_t *)nullptr);
        case ValueType::Float32:
            return f((float *)nullptr);
        case ValueType::Float64:
            return f((double *)nullptr);
    }
    return f((int32_t *)nullptr);
}

namespace ValueFormat {

std::string format(const uint8_t *buf, ValueType type) {
    std::ostringstream ss;
    return dispatchByType(type, [&](auto tag) -> std::string {
        using T = typename std::remove_pointer<decltype(tag)>::type;
        T v;
        memcpy(&v, buf, sizeof(T));
        std::ostringstream os;
        if constexpr (std::is_same<T, int8_t>::value) {
            os << (int)v;
        } else if constexpr (std::is_same<T, uint8_t>::value) {
            os << (unsigned)v;
        } else if constexpr (std::is_same<T, int32_t>::value) {
            os << v << " (0x" << std::hex << (uint32_t)v << ")";
        } else if constexpr (std::is_same<T, float>::value) {
            os << std::fixed << std::setprecision(3) << v;
        } else if constexpr (std::is_same<T, double>::value) {
            os << std::fixed << std::setprecision(5) << v;
        } else {
            os << v;
        }
        return os.str();
    });
}

size_t parse(const std::string &input, ValueType type, uint8_t buf[8]) {
    memset(buf, 0, 8);
    if (input.empty())
        return 0;

    try {
        return dispatchByType(type, [&](auto tag) -> size_t {
            using T = typename std::remove_pointer<decltype(tag)>::type;
            T v;
            if constexpr (std::is_same<T, float>::value)
                v = std::stof(input);
            else if constexpr (std::is_same<T, double>::value)
                v = std::stod(input);
            else if constexpr (std::is_unsigned<T>::value)
                v = static_cast<T>(std::stoull(input));
            else
                v = static_cast<T>(std::stoll(input));
            memcpy(buf, &v, sizeof(T));
            return sizeof(T);
        });
    } catch (...) {
        return 0;
    }
}

ValueType fromTag(const std::string &tag) {
    if (tag == "int8")
        return ValueType::Int8;
    if (tag == "uint8")
        return ValueType::UInt8;
    if (tag == "int16")
        return ValueType::Int16;
    if (tag == "uint16")
        return ValueType::UInt16;
    if (tag == "int32")
        return ValueType::Int32;
    if (tag == "uint32")
        return ValueType::UInt32;
    if (tag == "int64")
        return ValueType::Int64;
    if (tag == "uint64")
        return ValueType::UInt64;
    if (tag == "float" || tag == "float32")
        return ValueType::Float32;
    if (tag == "double" || tag == "float64")
        return ValueType::Float64;
    return ValueType::Int32;
}

std::string toTag(ValueType type) {
    return valueTypeLabel(type);
}

} // namespace ValueFormat

// Compare two typed values pointed to by a and b. Returns -1/0/1 like memcmp.
// Public entry point declared in ShirayukiMemory.hpp.
int compareTypedBytes(const uint8_t *a, const uint8_t *b, ValueType type) {
    return dispatchByType(type, [&](auto tag) -> int {
        using T = std::remove_pointer_t<decltype(tag)>;
        T va, vb;
        memcpy(&va, a, sizeof(T));
        memcpy(&vb, b, sizeof(T));
        return (va > vb) - (va < vb);
    });
}

} // namespace Shirayuki
