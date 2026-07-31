// Value type metadata, tag vocabulary, and format/parse round-trips.
#include "ShirayukiMemory.hpp"
#include "syharness.hpp"

using namespace Shirayuki;

static const ValueType kAllTypes[] = {
    ValueType::Int8,   ValueType::UInt8, ValueType::Int16,  ValueType::UInt16,  ValueType::Int32,
    ValueType::UInt32, ValueType::Int64, ValueType::UInt64, ValueType::Float32, ValueType::Float64,
};

static void testSizes() {
    SY_CHECK_EQ(valueTypeSize(ValueType::Int8), 1u);
    SY_CHECK_EQ(valueTypeSize(ValueType::UInt8), 1u);
    SY_CHECK_EQ(valueTypeSize(ValueType::Int16), 2u);
    SY_CHECK_EQ(valueTypeSize(ValueType::UInt16), 2u);
    SY_CHECK_EQ(valueTypeSize(ValueType::Int32), 4u);
    SY_CHECK_EQ(valueTypeSize(ValueType::UInt32), 4u);
    SY_CHECK_EQ(valueTypeSize(ValueType::Int64), 8u);
    SY_CHECK_EQ(valueTypeSize(ValueType::UInt64), 8u);
    SY_CHECK_EQ(valueTypeSize(ValueType::Float32), 4u);
    SY_CHECK_EQ(valueTypeSize(ValueType::Float64), 8u);

    // No type may exceed the 8-byte staging buffers used throughout the code.
    for (ValueType t : kAllTypes)
        SY_CHECK(valueTypeSize(t) <= 8u);
}

// fromTag(toTag(t)) must be the identity for every type. Before the descriptor
// table this failed for 9 of 10 types: toTag emitted "i32"/"f32" while fromTag
// only recognised "int32"/"float" and defaulted everything else to Int32.
static void testTagRoundTrip() {
    for (ValueType t : kAllTypes) {
        std::string tag = ValueFormat::toTag(t);
        ValueType back = ValueFormat::fromTag(tag);
        SY_CHECK_EQ(static_cast<int>(back), static_cast<int>(t));
    }
}

// Both the canonical long tags and the short display labels must resolve.
static void testTagVocabularies() {
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("int32")),
                static_cast<int>(ValueType::Int32));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("i32")), static_cast<int>(ValueType::Int32));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("float")),
                static_cast<int>(ValueType::Float32));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("f32")),
                static_cast<int>(ValueType::Float32));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("double")),
                static_cast<int>(ValueType::Float64));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("f64")),
                static_cast<int>(ValueType::Float64));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("uint64")),
                static_cast<int>(ValueType::UInt64));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("u64")), static_cast<int>(ValueType::UInt64));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("int8")), static_cast<int>(ValueType::Int8));
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("i8")), static_cast<int>(ValueType::Int8));
}

static void testParseFormatRoundTrip() {
    uint8_t buf[8];

    SY_CHECK_EQ(ValueFormat::parse("-42", ValueType::Int32, buf), 4u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::Int32), std::string("-42"));

    SY_CHECK_EQ(ValueFormat::parse("4294967295", ValueType::UInt32, buf), 4u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::UInt32), std::string("4294967295"));

    SY_CHECK_EQ(ValueFormat::parse("-9223372036854775808", ValueType::Int64, buf), 8u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::Int64), std::string("-9223372036854775808"));

    SY_CHECK_EQ(ValueFormat::parse("18446744073709551615", ValueType::UInt64, buf), 8u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::UInt64), std::string("18446744073709551615"));

    SY_CHECK_EQ(ValueFormat::parse("-128", ValueType::Int8, buf), 1u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::Int8), std::string("-128"));

    SY_CHECK_EQ(ValueFormat::parse("255", ValueType::UInt8, buf), 1u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::UInt8), std::string("255"));

    // Float values must survive a parse->format->parse cycle bit-exactly.
    SY_CHECK_EQ(ValueFormat::parse("1.5", ValueType::Float32, buf), 4u);
    float f = 0;
    memcpy(&f, buf, 4);
    SY_CHECK_EQ(f, 1.5f);

    SY_CHECK_EQ(ValueFormat::parse("-0.25", ValueType::Float64, buf), 8u);
    double d = 0;
    memcpy(&d, buf, 8);
    SY_CHECK_EQ(d, -0.25);
}

// parse() must report failure rather than throwing. Input comes straight from a
// UITextField, and an exception escaping a C++ TU through ObjC frames calls
// std::terminate, killing the host app.
static void testParseRejectsGarbage() {
    uint8_t buf[8];
    const char *garbage[] = {"", "abc", "0x", "1.2.3", "--5", " ", "+-1", "1e", "?"};

    for (const char *g : garbage) {
        size_t n = 0;
        bool threw = false;
        try {
            n = ValueFormat::parse(g, ValueType::Int32, buf);
        } catch (...) {
            threw = true;
        }
        SY_CHECK(!threw);
        SY_CHECK_EQ(n, 0u);
    }

    // Out-of-range must also be rejected, not silently wrapped.
    for (const char *g : {"99999999999999999999999999", "-99999999999999999999999999"}) {
        size_t n = 0;
        bool threw = false;
        try {
            n = ValueFormat::parse(g, ValueType::Int32, buf);
        } catch (...) {
            threw = true;
        }
        SY_CHECK(!threw);
        SY_CHECK_EQ(n, 0u);
    }

    // Trailing garbage after a valid number must be rejected too — "12abc"
    // silently becoming 12 is how a typo turns into a wrong memory write.
    size_t n = 0;
    try {
        n = ValueFormat::parse("12abc", ValueType::Int32, buf);
    } catch (...) {
        n = 999;
    }
    SY_CHECK_EQ(n, 0u);
}

static void testParseFailureLeavesBufferZeroed() {
    uint8_t buf[8];
    memset(buf, 0xAA, sizeof(buf));
    ValueFormat::parse("nonsense", ValueType::Int64, buf);
    for (size_t i = 0; i < 8; i++)
        SY_CHECK_EQ(static_cast<int>(buf[i]), 0);
}

// format() output must feed straight back into parse(). This is what lets the
// modify dialog prefill an edit field with the current value, and what keeps
// exported JSON reloadable. formatDisplay() is explicitly NOT round-trippable.
static void testFormatIsRoundTrippable() {
    const char *samples[] = {"0", "1", "-1", "127", "42"};

    for (ValueType t : kAllTypes) {
        for (const char *s : samples) {
            uint8_t a[8] = {}, b[8] = {};
            size_t n = ValueFormat::parse(s, t, a);
            if (n == 0)
                continue; // out of range for this type, e.g. -1 for uint
            std::string text = ValueFormat::format(a, t);
            size_t m = ValueFormat::parse(text, t, b);
            SY_CHECK_EQ(m, n);
            SY_CHECK_EQ(memcmp(a, b, n), 0);
        }
    }

    // Floats must survive exactly, including values with no short decimal form.
    for (double v : {0.1, -1.0 / 3.0, 1e-40, 123456.789}) {
        uint8_t a[8] = {}, b[8] = {};
        memcpy(a, &v, 8);
        std::string text = ValueFormat::format(a, ValueType::Float64);
        SY_CHECK_EQ(ValueFormat::parse(text, ValueType::Float64, b), 8u);
        SY_CHECK_EQ(memcmp(a, b, 8), 0);
    }
    for (float v : {0.1f, -1.0f / 3.0f, 123456.79f}) {
        uint8_t a[8] = {}, b[8] = {};
        memcpy(a, &v, 4);
        std::string text = ValueFormat::format(a, ValueType::Float32);
        SY_CHECK_EQ(ValueFormat::parse(text, ValueType::Float32, b), 4u);
        SY_CHECK_EQ(memcmp(a, b, 4), 0);
    }
}

static void testFormatDisplayAnnotatesIntegers() {
    uint8_t buf[8] = {};
    ValueFormat::parse("42", ValueType::Int32, buf);
    SY_CHECK_EQ(ValueFormat::formatDisplay(buf, ValueType::Int32), std::string("42 (0x2A)"));
    // Plain format must stay undecorated for the same bytes.
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::Int32), std::string("42"));

    ValueFormat::parse("-1", ValueType::Int8, buf);
    SY_CHECK_EQ(ValueFormat::formatDisplay(buf, ValueType::Int8), std::string("-1 (0xFF)"));

    ValueFormat::parse("1.5", ValueType::Float32, buf);
    SY_CHECK_EQ(ValueFormat::formatDisplay(buf, ValueType::Float32), std::string("1.500"));
}

// Hexadecimal entry is accepted, since addresses and flag values are habitually
// typed that way.
static void testParseAcceptsHex() {
    uint8_t buf[8] = {};
    SY_CHECK_EQ(ValueFormat::parse("0xFF", ValueType::Int32, buf), 4u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::Int32), std::string("255"));
    SY_CHECK_EQ(ValueFormat::parse("-0x10", ValueType::Int32, buf), 4u);
    SY_CHECK_EQ(ValueFormat::format(buf, ValueType::Int32), std::string("-16"));
}

// Range limits must be enforced per type, not silently truncated to the low
// bytes: scanning for 300 as an int8 should report an error, not match 44.
static void testParseEnforcesRange() {
    uint8_t buf[8] = {};
    SY_CHECK_EQ(ValueFormat::parse("300", ValueType::Int8, buf), 0u);
    SY_CHECK_EQ(ValueFormat::parse("-129", ValueType::Int8, buf), 0u);
    SY_CHECK_EQ(ValueFormat::parse("256", ValueType::UInt8, buf), 0u);
    SY_CHECK_EQ(ValueFormat::parse("-1", ValueType::UInt8, buf), 0u);
    SY_CHECK_EQ(ValueFormat::parse("-1", ValueType::UInt64, buf), 0u);
    SY_CHECK_EQ(ValueFormat::parse("65536", ValueType::UInt16, buf), 0u);
    SY_CHECK_EQ(ValueFormat::parse("4294967296", ValueType::UInt32, buf), 0u);

    // Exact boundaries must still be accepted.
    SY_CHECK_EQ(ValueFormat::parse("127", ValueType::Int8, buf), 1u);
    SY_CHECK_EQ(ValueFormat::parse("-128", ValueType::Int8, buf), 1u);
    SY_CHECK_EQ(ValueFormat::parse("255", ValueType::UInt8, buf), 1u);
}

// compareTypedBytes must order by numeric value. A bytewise compare gets this wrong
// for every multi-byte type on little-endian, which is what made the search
// tab's "increased" and "decreased" filters return the opposite of the truth.
static void testCompareValuesIsNumeric() {
    uint8_t lo[8] = {}, hi[8] = {};

    ValueFormat::parse("1", ValueType::Int32, lo);
    ValueFormat::parse("256", ValueType::Int32, hi);
    SY_CHECK(compareTypedBytes(hi, lo, ValueType::Int32) > 0);
    SY_CHECK(compareTypedBytes(lo, hi, ValueType::Int32) < 0);
    // The bytewise answer this replaces was the opposite.
    SY_CHECK(memcmp(hi, lo, 4) < 0);

    // Negative values: -1 is 0xFFFFFFFF, which sorts highest bytewise.
    ValueFormat::parse("-1", ValueType::Int32, lo);
    ValueFormat::parse("1", ValueType::Int32, hi);
    SY_CHECK(compareTypedBytes(lo, hi, ValueType::Int32) < 0);
    SY_CHECK(memcmp(lo, hi, 4) > 0);

    // Floats.
    ValueFormat::parse("-2.5", ValueType::Float32, lo);
    ValueFormat::parse("0.5", ValueType::Float32, hi);
    SY_CHECK(compareTypedBytes(lo, hi, ValueType::Float32) < 0);

    // Equality.
    ValueFormat::parse("7", ValueType::Int64, lo);
    ValueFormat::parse("7", ValueType::Int64, hi);
    SY_CHECK_EQ(compareTypedBytes(lo, hi, ValueType::Int64), 0);
}

static void testTryFromTagReportsUnknown() {
    ValueType t = ValueType::Float64;
    SY_CHECK(!ValueFormat::tryFromTag("f16", t));
    SY_CHECK(!ValueFormat::tryFromTag("", t));
    // A failed lookup must leave the caller's variable untouched rather than
    // quietly rewriting it to Int32.
    SY_CHECK_EQ(static_cast<int>(t), static_cast<int>(ValueType::Float64));
    SY_CHECK(ValueFormat::tryFromTag("i16", t));
    SY_CHECK_EQ(static_cast<int>(t), static_cast<int>(ValueType::Int16));

    // fromTag keeps the lenient Int32 fallback for legacy call sites.
    SY_CHECK_EQ(static_cast<int>(ValueFormat::fromTag("f16")), static_cast<int>(ValueType::Int32));
}

static void testAllTagsRoundTrip() {
    auto tags = ValueFormat::allTags();
    SY_CHECK_EQ(tags.size(), static_cast<size_t>(ValueType::Float64) + 1);
    for (const auto &tag : tags)
        SY_CHECK_EQ(ValueFormat::toTag(ValueFormat::fromTag(tag)), tag);
}

static void run() {
    testSizes();
    testTagRoundTrip();
    testTagVocabularies();
    testParseFormatRoundTrip();
    testParseRejectsGarbage();
    testParseFailureLeavesBufferZeroed();
    testFormatIsRoundTrippable();
    testFormatDisplayAnnotatesIntegers();
    testParseAcceptsHex();
    testParseEnforcesRange();
    testCompareValuesIsNumeric();
    testTryFromTagReportsUnknown();
    testAllTagsRoundTrip();
}

SY_MAIN("test_valuetype")
