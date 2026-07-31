// Value type metadata, parsing, formatting and comparison.
//
// Everything type-dependent lives in one descriptor table. Adding a ValueType
// means adding one row here and one enumerator to the enum — previously the
// same 10-arm switch was spelled out in six places (size, label, compare,
// format, parse, tag lookup) plus three more in the GUI layer, and they had
// already drifted apart from each other.

#include "ShirayukiConfig.hpp"
#include "ShirayukiMemory.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace Shirayuki {
namespace {

// --- Scalar helpers -------------------------------------------------------

std::string trimmed(const std::string &s) {
    const char *ws = " \t\r\n\f\v";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// Parse an entire string as an integer of type T. Accepts an optional sign and
// an optional 0x/0X prefix for hexadecimal. Returns false for empty input,
// stray characters anywhere (including trailing), and out-of-range values.
//
// Never throws: input arrives straight from a UITextField, and an exception
// escaping this translation unit through ObjC frames calls std::terminate.
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
// strtod/strtof are used with an explicit end pointer and errno check.
template <typename T> bool parseFloating(const std::string &in, T &out) {
    static_assert(std::is_floating_point<T>::value, "parseFloating requires a float type");

    const std::string s = trimmed(in);
    if (s.empty())
        return false;

    errno = 0;
    char *endp = nullptr;
    T v;
    if (std::is_same<T, float>::value)
        v = static_cast<T>(std::strtof(s.c_str(), &endp));
    else
        v = static_cast<T>(std::strtod(s.c_str(), &endp));

    if (endp != s.c_str() + s.size())
        return false;
    if (errno == ERANGE)
        return false;

    out = v;
    return true;
}

// --- Descriptor operations ------------------------------------------------

template <typename T> size_t opParseIntegral(const std::string &in, uint8_t *buf) {
    T v{};
    if (!parseIntegral<T>(in, v))
        return 0;
    memcpy(buf, &v, sizeof(T));
    return sizeof(T);
}

template <typename T> size_t opParseFloating(const std::string &in, uint8_t *buf) {
    T v{};
    if (!parseFloating<T>(in, v))
        return 0;
    memcpy(buf, &v, sizeof(T));
    return sizeof(T);
}

template <typename T> std::string opFormatIntegral(const uint8_t *buf) {
    T v{};
    memcpy(&v, buf, sizeof(T));
    std::ostringstream ss;
    // Promote so int8_t/uint8_t print as numbers rather than characters.
    if constexpr (std::is_signed<T>::value)
        ss << static_cast<long long>(v);
    else
        ss << static_cast<unsigned long long>(v);
    return ss.str();
}

// Round-trippable: max_digits10 guarantees parse(format(x)) == x bit-exactly.
template <typename T> std::string opFormatFloating(const uint8_t *buf) {
    T v{};
    memcpy(&v, buf, sizeof(T));
    std::ostringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << v;
    return ss.str();
}

template <typename T> std::string opDisplayIntegral(const uint8_t *buf) {
    T v{};
    memcpy(&v, buf, sizeof(T));
    using U = typename std::make_unsigned<T>::type;
    std::ostringstream ss;
    ss << opFormatIntegral<T>(buf) << " (0x" << std::hex << std::uppercase
       << static_cast<unsigned long long>(static_cast<U>(v)) << ")";
    return ss.str();
}

// Fixed notation is easier to scan in a table cell than max_digits10 output.
template <typename T, int Precision> std::string opDisplayFloating(const uint8_t *buf) {
    T v{};
    memcpy(&v, buf, sizeof(T));
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(Precision) << v;
    return ss.str();
}

// Typed three-way compare. Doing this bytewise is what made "increased" and
// "decreased" wrong for every multi-byte type on little-endian ARM64.
template <typename T> int opCompare(const uint8_t *a, const uint8_t *b) {
    T x{}, y{};
    memcpy(&x, a, sizeof(T));
    memcpy(&y, b, sizeof(T));
    if (x < y)
        return -1;
    if (y < x)
        return 1;
    return 0;
}

struct TypeDesc {
    ValueType type;
    const char *canonicalTag; // "int32" — stable identifier for JSON/sessions
    const char *shortTag;     // "i32"   — compact UI label
    const char *altTag;       // "float" / "float64" alias, or nullptr
    size_t size;
    size_t (*parse)(const std::string &, uint8_t *);
    std::string (*format)(const uint8_t *);
    std::string (*display)(const uint8_t *);
    int (*compare)(const uint8_t *, const uint8_t *);
};

// clang-format off
constexpr TypeDesc kTypes[] = {
    {ValueType::Int8,    "int8",   "i8",  nullptr,   1,
     opParseIntegral<int8_t>,   opFormatIntegral<int8_t>,   opDisplayIntegral<int8_t>,   opCompare<int8_t>},
    {ValueType::UInt8,   "uint8",  "u8",  nullptr,   1,
     opParseIntegral<uint8_t>,  opFormatIntegral<uint8_t>,  opDisplayIntegral<uint8_t>,  opCompare<uint8_t>},
    {ValueType::Int16,   "int16",  "i16", nullptr,   2,
     opParseIntegral<int16_t>,  opFormatIntegral<int16_t>,  opDisplayIntegral<int16_t>,  opCompare<int16_t>},
    {ValueType::UInt16,  "uint16", "u16", nullptr,   2,
     opParseIntegral<uint16_t>, opFormatIntegral<uint16_t>, opDisplayIntegral<uint16_t>, opCompare<uint16_t>},
    {ValueType::Int32,   "int32",  "i32", nullptr,   4,
     opParseIntegral<int32_t>,  opFormatIntegral<int32_t>,  opDisplayIntegral<int32_t>,  opCompare<int32_t>},
    {ValueType::UInt32,  "uint32", "u32", nullptr,   4,
     opParseIntegral<uint32_t>, opFormatIntegral<uint32_t>, opDisplayIntegral<uint32_t>, opCompare<uint32_t>},
    {ValueType::Int64,   "int64",  "i64", nullptr,   8,
     opParseIntegral<int64_t>,  opFormatIntegral<int64_t>,  opDisplayIntegral<int64_t>,  opCompare<int64_t>},
    {ValueType::UInt64,  "uint64", "u64", nullptr,   8,
     opParseIntegral<uint64_t>, opFormatIntegral<uint64_t>, opDisplayIntegral<uint64_t>, opCompare<uint64_t>},
    {ValueType::Float32, "float",  "f32", "float32", 4,
     opParseFloating<float>,    opFormatFloating<float>,    opDisplayFloating<float, 3>,  opCompare<float>},
    {ValueType::Float64, "double", "f64", "float64", 8,
     opParseFloating<double>,   opFormatFloating<double>,   opDisplayFloating<double, 5>, opCompare<double>},
};
// clang-format on

constexpr size_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);

// The table must stay aligned with the enum: one row per enumerator, in order,
// so index == static_cast<size_t>(type).
static_assert(kTypeCount == static_cast<size_t>(ValueType::Float64) + 1,
              "kTypes must have one row per ValueType enumerator");

// Buffers all over the codebase are uint8_t[kMaxValueSize]; a wider type would
// silently overflow them.
constexpr size_t widestType() {
    size_t w = 0;
    for (size_t i = 0; i < kTypeCount; i++)
        if (kTypes[i].size > w)
            w = kTypes[i].size;
    return w;
}
static_assert(widestType() == kMaxValueSize, "kMaxValueSize must match the widest ValueType");

const TypeDesc &desc(ValueType type) {
    const size_t i = static_cast<size_t>(type);
    return i < kTypeCount ? kTypes[i] : kTypes[static_cast<size_t>(ValueType::Int32)];
}

bool tagMatches(const TypeDesc &d, const std::string &tag) {
    return tag == d.canonicalTag || tag == d.shortTag || (d.altTag && tag == d.altTag);
}

} // namespace

// --- Public API -----------------------------------------------------------

size_t valueTypeSize(ValueType type) {
    return desc(type).size;
}

std::string valueTypeLabel(ValueType type) {
    return desc(type).shortTag;
}

int compareValues(const uint8_t *a, const uint8_t *b, ValueType type) {
    if (!a || !b)
        return 0;
    return desc(type).compare(a, b);
}

namespace ValueFormat {

std::string format(const uint8_t *buf, ValueType type) {
    if (!buf)
        return {};
    return desc(type).format(buf);
}

std::string formatDisplay(const uint8_t *buf, ValueType type) {
    if (!buf)
        return {};
    return desc(type).display(buf);
}

size_t parse(const std::string &input, ValueType type, uint8_t buf[kMaxValueSize]) {
    if (!buf)
        return 0;
    memset(buf, 0, kMaxValueSize);
    return desc(type).parse(input, buf);
}

bool tryFromTag(const std::string &tag, ValueType &out) {
    const std::string t = trimmed(tag);
    for (size_t i = 0; i < kTypeCount; i++) {
        if (tagMatches(kTypes[i], t)) {
            out = kTypes[i].type;
            return true;
        }
    }
    return false;
}

ValueType fromTag(const std::string &tag) {
    ValueType out = ValueType::Int32;
    tryFromTag(tag, out);
    return out;
}

std::string toTag(ValueType type) {
    return desc(type).canonicalTag;
}

std::vector<std::string> allTags() {
    std::vector<std::string> tags;
    tags.reserve(kTypeCount);
    for (size_t i = 0; i < kTypeCount; i++)
        tags.push_back(kTypes[i].canonicalTag);
    return tags;
}

} // namespace ValueFormat
} // namespace Shirayuki
