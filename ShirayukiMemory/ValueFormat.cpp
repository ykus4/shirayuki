#include "ShirayukiMemory.hpp"
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
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
    return kDefaultValueSize;
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

namespace {

// --- Tag vocabulary -------------------------------------------------------

// One row per ValueType, in enumerator order. `canonical` is the stable
// identifier written to sessions and JSON; `shortTag` mirrors valueTypeLabel so
// a tag produced anywhere in the UI still resolves; `alt` covers the legacy
// "float32"/"float64" spellings.
struct TagRow {
    ValueType type;
    const char *canonical;
    const char *shortTag;
    const char *alt;
};

constexpr TagRow kTagRows[] = {
    {ValueType::Int8, "int8", "i8", nullptr},
    {ValueType::UInt8, "uint8", "u8", nullptr},
    {ValueType::Int16, "int16", "i16", nullptr},
    {ValueType::UInt16, "uint16", "u16", nullptr},
    {ValueType::Int32, "int32", "i32", nullptr},
    {ValueType::UInt32, "uint32", "u32", nullptr},
    {ValueType::Int64, "int64", "i64", nullptr},
    {ValueType::UInt64, "uint64", "u64", nullptr},
    {ValueType::Float32, "float", "f32", "float32"},
    {ValueType::Float64, "double", "f64", "float64"},
};

constexpr size_t kTagRowCount = sizeof(kTagRows) / sizeof(kTagRows[0]);

// The table must stay aligned with the enum: one row per enumerator, in order,
// so index == static_cast<size_t>(type).
static_assert(kTagRowCount == static_cast<size_t>(ValueType::Float64) + 1,
              "kTagRows must have one row per ValueType enumerator");

const TagRow &tagRow(ValueType type) {
    const size_t i = static_cast<size_t>(type);
    return i < kTagRowCount ? kTagRows[i] : kTagRows[static_cast<size_t>(ValueType::Int32)];
}

std::string trimmed(const std::string &s) {
    const char *ws = " \t\r\n\f\v";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// --- Parsing --------------------------------------------------------------

// Parse an entire string as an integer of type T. Accepts an optional sign and
// an optional 0x/0X prefix for hexadecimal. Returns false for empty input,
// stray characters anywhere (including trailing), and out-of-range values.
//
// std::stoll/stoull silently accepted trailing garbage ("12abc" parsed as 12)
// and range-checked only against the widest type, so a value of 300 written into
// an int8 truncated to 44 instead of being refused. Never throws: input arrives
// straight from a UITextField, and an exception escaping this translation unit
// through ObjC frames calls std::terminate.
template <typename T> bool parseIntegral(const std::string &in, T &out) {
    static_assert(std::is_integral<T>::value, "parseIntegral requires an integral type");

    const std::string s = trimmed(in);
    if (s.empty())
        return false;

    size_t i = 0;
    bool negative = false;
    if (s[i] == '+' || s[i] == '-') {
        negative = (s[i] == '-');
        ++i;
    }
    if (negative && !std::is_signed<T>::value)
        return false;

    int base = 10;
    if (i + 2 <= s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }
    if (i >= s.size())
        return false;

    // from_chars rejects leading whitespace, signs and stray characters without
    // throwing, which is exactly the contract we need.
    unsigned long long magnitude = 0;
    const char *begin = s.data() + i;
    const char *end = s.data() + s.size();
    auto res = std::from_chars(begin, end, magnitude, base);
    if (res.ec != std::errc() || res.ptr != end)
        return false;

    if (negative) {
        // |min| for a signed T is max + 1, computed in unsigned arithmetic so
        // INT64_MIN does not overflow on the way.
        const unsigned long long limit =
            static_cast<unsigned long long>(std::numeric_limits<T>::max()) + 1ull;
        if (magnitude > limit)
            return false;
        out = static_cast<T>(0ull - magnitude);
        return true;
    }

    if (magnitude > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
        return false;
    out = static_cast<T>(magnitude);
    return true;
}

// Parse an entire string as a floating point value. std::from_chars for
// floating point is not available across the libc++ versions this targets, so
// strtof/strtod are used with an explicit end pointer and an errno check.
template <typename T> bool parseFloating(const std::string &in, T &out) {
    static_assert(std::is_floating_point<T>::value, "parseFloating requires a float type");

    const std::string s = trimmed(in);
    if (s.empty())
        return false;

    errno = 0;
    char *endp = nullptr;
    T v;
    if constexpr (std::is_same<T, float>::value)
        v = std::strtof(s.c_str(), &endp);
    else
        v = std::strtod(s.c_str(), &endp);

    if (endp != s.c_str() + s.size())
        return false;
    if (errno == ERANGE)
        return false;

    out = v;
    return true;
}

} // namespace

namespace ValueFormat {

std::string format(const uint8_t *buf, ValueType type) {
    if (!buf)
        return {};
    return dispatchByType(type, [&](auto tag) -> std::string {
        using T = typename std::remove_pointer<decltype(tag)>::type;
        T v{};
        memcpy(&v, buf, sizeof(T));
        std::ostringstream os;
        if constexpr (std::is_floating_point<T>::value) {
            // max_digits10 is what makes parse(format(x)) == x bit-exact; fixed
            // notation at 3 or 5 decimals silently rounded the value away.
            os << std::setprecision(std::numeric_limits<T>::max_digits10) << v;
        } else if constexpr (std::is_signed<T>::value) {
            // Promote so int8_t/uint8_t print as numbers rather than characters.
            os << static_cast<long long>(v);
        } else {
            os << static_cast<unsigned long long>(v);
        }
        return os.str();
    });
}

std::string formatDisplay(const uint8_t *buf, ValueType type) {
    if (!buf)
        return {};
    return dispatchByType(type, [&](auto tag) -> std::string {
        using T = typename std::remove_pointer<decltype(tag)>::type;
        T v{};
        memcpy(&v, buf, sizeof(T));
        std::ostringstream os;
        if constexpr (std::is_same<T, float>::value) {
            // Fixed notation is easier to scan in a table cell than the
            // round-trippable form.
            os << std::fixed << std::setprecision(3) << v;
        } else if constexpr (std::is_same<T, double>::value) {
            os << std::fixed << std::setprecision(5) << v;
        } else {
            // Hex annotation for every integer type, not just Int32. Going
            // through the unsigned counterpart makes -1 read as 0xFF rather
            // than a sign-extended 0xFFFFFFFFFFFFFFFF.
            using U = typename std::make_unsigned<T>::type;
            os << format(buf, type) << " (0x" << std::hex << std::uppercase
               << static_cast<unsigned long long>(static_cast<U>(v)) << ")";
        }
        return os.str();
    });
}

size_t parse(const std::string &input, ValueType type, uint8_t buf[kMaxValueSize]) {
    if (!buf)
        return 0;
    memset(buf, 0, kMaxValueSize);

    return dispatchByType(type, [&](auto tag) -> size_t {
        using T = typename std::remove_pointer<decltype(tag)>::type;
        T v{};
        if constexpr (std::is_floating_point<T>::value) {
            if (!parseFloating<T>(input, v))
                return 0;
        } else {
            if (!parseIntegral<T>(input, v))
                return 0;
        }
        memcpy(buf, &v, sizeof(T));
        return sizeof(T);
    });
}

bool tryFromTag(const std::string &tag, ValueType &out) {
    const std::string t = trimmed(tag);
    if (t.empty())
        return false;
    for (size_t i = 0; i < kTagRowCount; i++) {
        const TagRow &row = kTagRows[i];
        if (t == row.canonical || t == row.shortTag || (row.alt && t == row.alt)) {
            out = row.type;
            return true;
        }
    }
    return false;
}

ValueType fromTag(const std::string &tag) {
    // Lenient by design: legacy call sites pass a tag straight from a session
    // file or a UI label and expect a usable type back.
    ValueType out = ValueType::Int32;
    tryFromTag(tag, out);
    return out;
}

std::string toTag(ValueType type) {
    // The canonical tag, not valueTypeLabel: returning the short label made
    // fromTag(toTag(t)) collapse 9 of 10 types to Int32.
    return tagRow(type).canonical;
}

std::vector<std::string> allTags() {
    std::vector<std::string> tags;
    tags.reserve(kTagRowCount);
    for (size_t i = 0; i < kTagRowCount; i++)
        tags.push_back(kTagRows[i].canonical);
    return tags;
}

} // namespace ValueFormat

// Compare two typed values pointed to by a and b. Returns -1/0/1 like memcmp.
// Public entry point declared in ShirayukiMemory.hpp.
int compareTypedBytes(const uint8_t *a, const uint8_t *b, ValueType type) {
    if (!a || !b)
        return 0;
    return dispatchByType(type, [&](auto tag) -> int {
        using T = std::remove_pointer_t<decltype(tag)>;
        T va, vb;
        memcpy(&va, a, sizeof(T));
        memcpy(&vb, b, sizeof(T));
        return (va > vb) - (va < vb);
    });
}

} // namespace Shirayuki
