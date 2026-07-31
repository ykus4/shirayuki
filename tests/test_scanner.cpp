// Value/pattern/string/regex scanning and result narrowing.
#include "ShirayukiMemory.hpp"
#include "syharness.hpp"

#include <cstring>
#include <string>
#include <sys/mman.h>
#include <vector>

using namespace Shirayuki;

namespace {

// A scratch buffer to scan, kept off the stack so its address is stable and its
// contents are not surrounded by unrelated matching bytes.
struct Arena {
    uint8_t *base = nullptr;
    size_t size = 0;

    explicit Arena(size_t bytes) : size(bytes) {
        base = static_cast<uint8_t *>(
            mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
        if (base == MAP_FAILED)
            base = nullptr;
        else
            memset(base, 0, bytes);
    }
    ~Arena() {
        if (base)
            munmap(base, size);
    }
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    uintptr_t addr(size_t offset = 0) const {
        return reinterpret_cast<uintptr_t>(base) + offset;
    }
};

bool contains(const std::vector<uintptr_t> &v, uintptr_t x) {
    for (uintptr_t e : v)
        if (e == x)
            return true;
    return false;
}

} // namespace

static void testFindValueAligned() {
    Arena arena(4096);
    SY_CHECK(arena.base != nullptr);
    if (!arena.base)
        return;

    const int32_t needle = 0x41424344;
    memcpy(arena.base + 0, &needle, 4);
    memcpy(arena.base + 64, &needle, 4);
    memcpy(arena.base + 2048, &needle, 4);

    auto hits = Scanner::findValue<int32_t>(arena.addr(), arena.size, needle);
    SY_CHECK_EQ(hits.size(), 3u);
    SY_CHECK(contains(hits, arena.addr(0)));
    SY_CHECK(contains(hits, arena.addr(64)));
    SY_CHECK(contains(hits, arena.addr(2048)));
}

// A typed scan strides by the value width, so a value written at an unaligned
// offset is deliberately invisible. Byte-granular callers pass stride 1.
static void testFindValueStride() {
    Arena arena(4096);
    if (!arena.base)
        return;

    const int32_t needle = 0x11223344;
    memcpy(arena.base + 6, &needle, 4); // not 4-byte aligned

    uint8_t bytes[4];
    memcpy(bytes, &needle, 4);

    auto aligned = Scanner::findValueBytes(arena.addr(), arena.size, bytes, 4);
    SY_CHECK_EQ(aligned.size(), 0u);

    auto unaligned = Scanner::findValueBytes(arena.addr(), arena.size, bytes, 4, 1);
    SY_CHECK_EQ(unaligned.size(), 1u);
    SY_CHECK(contains(unaligned, arena.addr(6)));
}

// Chunked reads must not lose a match that straddles a chunk boundary, nor
// report one twice. kScanChunkSize is 1 MB, so the arena has to span one.
static void testFindValueAcrossChunkBoundary() {
    const size_t size = kScanChunkSize + 8192;
    Arena arena(size);
    if (!arena.base)
        return;

    const int64_t needle = 0x0102030405060708LL;
    uint8_t bytes[8];
    memcpy(bytes, &needle, 8);

    // Straddling the boundary, and just inside each side of it.
    const size_t straddle = kScanChunkSize - 4;
    memcpy(arena.base + straddle, bytes, 8);
    memcpy(arena.base + kScanChunkSize - 64, bytes, 8);
    memcpy(arena.base + kScanChunkSize + 64, bytes, 8);

    auto hits = Scanner::findValueBytes(arena.addr(), arena.size, bytes, 8, 1);
    SY_CHECK(contains(hits, arena.addr(straddle)));
    SY_CHECK(contains(hits, arena.addr(kScanChunkSize - 64)));
    SY_CHECK(contains(hits, arena.addr(kScanChunkSize + 64)));

    // No duplicates: each address must appear exactly once.
    size_t straddleCount = 0;
    for (uintptr_t a : hits)
        if (a == arena.addr(straddle))
            straddleCount++;
    SY_CHECK_EQ(straddleCount, 1u);
}

static void testFindPattern() {
    Arena arena(4096);
    if (!arena.base)
        return;

    const uint8_t seq[] = {0xDE, 0xAD, 0xBE, 0xEF};
    memcpy(arena.base + 100, seq, 4);
    memcpy(arena.base + 500, seq, 4);
    // A near miss that only the wildcard form should match.
    const uint8_t nearMiss[] = {0xDE, 0x99, 0xBE, 0xEF};
    memcpy(arena.base + 900, nearMiss, 4);

    auto exact = Scanner::findPattern(arena.addr(), arena.size, "DE AD BE EF");
    SY_CHECK_EQ(exact.size(), 2u);
    SY_CHECK(contains(exact, arena.addr(100)));
    SY_CHECK(contains(exact, arena.addr(500)));

    auto wild = Scanner::findPattern(arena.addr(), arena.size, "DE ?? BE EF");
    SY_CHECK_EQ(wild.size(), 3u);
    SY_CHECK(contains(wild, arena.addr(900)));

    // Malformed patterns must yield nothing rather than matching everything.
    SY_CHECK_EQ(Scanner::findPattern(arena.addr(), arena.size, "ZZ").size(), 0u);
    SY_CHECK_EQ(Scanner::findPattern(arena.addr(), arena.size, "").size(), 0u);

    uintptr_t first = Scanner::findPatternFirst(arena.addr(), arena.size, "DE AD BE EF");
    SY_CHECK_EQ(first, arena.addr(100));
}

static void testFindString() {
    Arena arena(4096);
    if (!arena.base)
        return;

    const char *needle = "ShirayukiTestNeedle";
    memcpy(arena.base + 32, needle, strlen(needle) + 1);
    memcpy(arena.base + 1024, needle, strlen(needle) + 1);

    auto hits = Scanner::findString(arena.addr(), arena.size, needle);
    SY_CHECK_EQ(hits.size(), 2u);
    SY_CHECK(contains(hits, arena.addr(32)));
    SY_CHECK(contains(hits, arena.addr(1024)));

    SY_CHECK_EQ(Scanner::findString(arena.addr(), arena.size, "").size(), 0u);
    SY_CHECK_EQ(Scanner::findString(arena.addr(), arena.size, "NotPresentAnywhere").size(), 0u);
}

static void testFindRegex() {
    Arena arena(4096);
    if (!arena.base)
        return;

    const char *a = "player_health=100";
    const char *b = "player_mana=50";
    const char *c = "unrelated";
    memcpy(arena.base + 16, a, strlen(a) + 1);
    memcpy(arena.base + 128, b, strlen(b) + 1);
    memcpy(arena.base + 256, c, strlen(c) + 1);

    auto hits = Scanner::findRegex(arena.addr(), arena.size, "^player_[a-z]+=[0-9]+$");
    SY_CHECK_EQ(hits.size(), 2u);
    SY_CHECK(contains(hits, arena.addr(16)));
    SY_CHECK(contains(hits, arena.addr(128)));

    // An invalid regex must fail closed, not throw out of the library.
    bool threw = false;
    try {
        auto bad = Scanner::findRegex(arena.addr(), arena.size, "([unclosed");
        SY_CHECK_EQ(bad.size(), 0u);
    } catch (...) {
        threw = true;
    }
    SY_CHECK(!threw);
}

// Scanning a range that is unmapped partway through must not fault. This is the
// scenario the chunked-copy rewrite exists for.
static void testScanSurvivesUnmappedRange() {
    const size_t size = 3 * 4096;
    void *page = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    SY_CHECK(page != MAP_FAILED);
    if (page == MAP_FAILED)
        return;

    const uintptr_t base = reinterpret_cast<uintptr_t>(page);
    munmap(page, size);

    const uint8_t needle[4] = {1, 2, 3, 4};
    auto hits = Scanner::findValueBytes(base, size, needle, 4);
    SY_CHECK_EQ(hits.size(), 0u);

    auto pat = Scanner::findPattern(base, size, "01 02 03 04");
    SY_CHECK_EQ(pat.size(), 0u);
}

// The headline narrowing bug: memcmp on little-endian bytes made "increased" and
// "decreased" report the opposite of the truth for every multi-byte type.
static void testNarrowIncreasedDecreased() {
    Arena arena(4096);
    if (!arena.base)
        return;

    // Three int32 slots with known starting values.
    const int32_t initial[3] = {1, 100, -5};
    for (size_t i = 0; i < 3; i++)
        memcpy(arena.base + i * 4, &initial[i], 4);

    std::vector<Scanner::Candidate> candidates;
    for (size_t i = 0; i < 3; i++) {
        Scanner::Candidate c;
        c.address = arena.addr(i * 4);
        c.snapshotValue.assign(arena.base + i * 4, arena.base + i * 4 + 4);
        candidates.push_back(c);
    }

    // slot 0: 1 -> 256   (increased; memcmp would call this a decrease)
    // slot 1: 100 -> 99  (decreased)
    // slot 2: -5 -> -5   (unchanged)
    const int32_t updated[3] = {256, 99, -5};
    for (size_t i = 0; i < 3; i++)
        memcpy(arena.base + i * 4, &updated[i], 4);

    auto increased = Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::Increased);
    SY_CHECK_EQ(increased.size(), 1u);
    if (increased.size() == 1)
        SY_CHECK_EQ(increased[0].address, arena.addr(0));

    auto decreased = Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::Decreased);
    SY_CHECK_EQ(decreased.size(), 1u);
    if (decreased.size() == 1)
        SY_CHECK_EQ(decreased[0].address, arena.addr(4));

    auto unchanged = Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::Unchanged);
    SY_CHECK_EQ(unchanged.size(), 1u);
    if (unchanged.size() == 1)
        SY_CHECK_EQ(unchanged[0].address, arena.addr(8));

    auto changed = Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::Changed);
    SY_CHECK_EQ(changed.size(), 2u);
}

// Signed and floating point ordering, where a bytewise compare is also wrong.
static void testNarrowSignedAndFloat() {
    Arena arena(4096);
    if (!arena.base)
        return;

    // int32: -1 -> 1 is an increase, but 0xFFFFFFFF sorts above 1 bytewise.
    const int32_t before = -1, after = 1;
    memcpy(arena.base, &before, 4);
    Scanner::Candidate c;
    c.address = arena.addr(0);
    c.snapshotValue.assign(arena.base, arena.base + 4);
    memcpy(arena.base, &after, 4);

    auto up = Scanner::narrowResults({c}, ValueType::Int32, CompareMode::Increased);
    SY_CHECK_EQ(up.size(), 1u);

    // float: -2.5 -> 0.5 is an increase.
    const float fBefore = -2.5f, fAfter = 0.5f;
    memcpy(arena.base + 16, &fBefore, 4);
    Scanner::Candidate fc;
    fc.address = arena.addr(16);
    fc.snapshotValue.assign(arena.base + 16, arena.base + 20);
    memcpy(arena.base + 16, &fAfter, 4);

    auto fUp = Scanner::narrowResults({fc}, ValueType::Float32, CompareMode::Increased);
    SY_CHECK_EQ(fUp.size(), 1u);
    auto fDown = Scanner::narrowResults({fc}, ValueType::Float32, CompareMode::Decreased);
    SY_CHECK_EQ(fDown.size(), 0u);
}

static void testNarrowExactAndThresholds() {
    Arena arena(4096);
    if (!arena.base)
        return;

    const int32_t values[4] = {10, 20, 30, 40};
    std::vector<Scanner::Candidate> candidates;
    for (size_t i = 0; i < 4; i++) {
        memcpy(arena.base + i * 4, &values[i], 4);
        Scanner::Candidate c;
        c.address = arena.addr(i * 4);
        c.snapshotValue.assign(arena.base + i * 4, arena.base + i * 4 + 4);
        candidates.push_back(c);
    }

    uint8_t target[kMaxValueSize] = {};
    ValueFormat::parse("30", ValueType::Int32, target);

    auto exact = Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::Exact, target);
    SY_CHECK_EQ(exact.size(), 1u);
    if (exact.size() == 1)
        SY_CHECK_EQ(exact[0].address, arena.addr(8));

    auto greater =
        Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::GreaterThan, target);
    SY_CHECK_EQ(greater.size(), 1u);

    auto less = Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::LessThan, target);
    SY_CHECK_EQ(less.size(), 2u);

    // Modes requiring a target must return nothing when it is absent, rather
    // than comparing against a null pointer.
    auto noTarget =
        Scanner::narrowResults(candidates, ValueType::Int32, CompareMode::Exact, nullptr);
    SY_CHECK_EQ(noTarget.size(), 0u);
}

// A snapshot narrower than the type would be read past its end.
static void testNarrowRejectsShortSnapshot() {
    Arena arena(4096);
    if (!arena.base)
        return;

    Scanner::Candidate c;
    c.address = arena.addr(0);
    c.snapshotValue.assign(2, 0); // only 2 bytes for an int32 narrow

    auto out = Scanner::narrowResults({c}, ValueType::Int32, CompareMode::Changed);
    SY_CHECK_EQ(out.size(), 0u);
}

static void run() {
    testFindValueAligned();
    testFindValueStride();
    testFindValueAcrossChunkBoundary();
    testFindPattern();
    testFindString();
    testFindRegex();
    testScanSurvivesUnmappedRange();
    testNarrowIncreasedDecreased();
    testNarrowSignedAndFloat();
    testNarrowExactAndThresholds();
    testNarrowRejectsShortSnapshot();
}

SY_MAIN("test_scanner")
