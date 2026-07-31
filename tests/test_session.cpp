// JSON reader/writer and session save/load round-trips.
//
// The previous implementation wrote with a hand-rolled serialiser and read with
// NSJSONSerialization, and `load` had no caller anywhere — so nothing had ever
// verified that a saved session could be read back at all. It could not: the
// writer left `\r`, `\b`, `\f` and control characters unescaped.
#include "Json.hpp"
#include "Session.hpp"
#include "syharness.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace Shirayuki;

namespace {

std::string tempPath(const char *name) {
    const char *dir = std::getenv("TMPDIR");
    std::string base = (dir && *dir) ? dir : "/tmp/";
    if (base.back() != '/')
        base += '/';
    return base + "shirayuki-test-" + name;
}

} // namespace

static void testJsonScalars() {
    Json::Value v;
    std::string err;

    SY_CHECK(Json::parse("null", v, &err));
    SY_CHECK(v.isNull());

    SY_CHECK(Json::parse("true", v, &err));
    SY_CHECK(v.asBool());

    SY_CHECK(Json::parse("false", v, &err));
    SY_CHECK(!v.asBool(true));

    SY_CHECK(Json::parse("42", v, &err));
    SY_CHECK_EQ(v.asNumber(), 42.0);
    SY_CHECK_EQ(v.serialize(), std::string("42"));

    SY_CHECK(Json::parse("-1.5", v, &err));
    SY_CHECK_EQ(v.asNumber(), -1.5);

    SY_CHECK(Json::parse("\"hello\"", v, &err));
    SY_CHECK_EQ(v.asString(), std::string("hello"));
}

static void testJsonRejectsMalformed() {
    Json::Value v;
    const char *bad[] = {
        "",
        "{",
        "}",
        "[",
        "]",
        "{\"a\"}",
        "{\"a\":}",
        "[1,]",
        "[1 2]",
        "tru",
        "\"unterminated",
        "{\"a\":1,}",
        "nan",
        "0x10",
        "{'a':1}",
        "[1,2,3",
    };
    for (const char *text : bad) {
        std::string err;
        const bool ok = Json::parse(text, v, &err);
        SY_CHECK(!ok);
        // A rejection must come with a reason, not just a false.
        if (!ok)
            SY_CHECK(!err.empty());
    }
}

// Every character that needs escaping must survive a write/read cycle. These are
// exactly the ones the old serialiser dropped.
static void testJsonEscapesRoundTrip() {
    const std::string nasty =
        std::string("quote\" backslash\\ slash/ newline\n tab\t return\r bell\b form\f "
                    "null-ish\x01\x02\x1F end");

    Json::Object o;
    o["text"] = Json::Value(nasty);
    const std::string encoded = Json::Value(o).serialize();

    Json::Value parsed;
    std::string err;
    SY_CHECK(Json::parse(encoded, parsed, &err));
    SY_CHECK_EQ(parsed["text"].asString(), nasty);
}

static void testJsonUnicodeEscapes() {
    Json::Value v;
    std::string err;

    SY_CHECK(Json::parse("\"\\u0041\\u0042\"", v, &err));
    SY_CHECK_EQ(v.asString(), std::string("AB"));

    // Multi-byte code point.
    SY_CHECK(Json::parse("\"\\u3042\"", v, &err));
    SY_CHECK_EQ(v.asString(), std::string("\xE3\x81\x82")); // U+3042 HIRAGANA A

    // Surrogate pair must combine into one code point, not two broken halves.
    SY_CHECK(Json::parse("\"\\uD83D\\uDE00\"", v, &err));
    SY_CHECK_EQ(v.asString(), std::string("\xF0\x9F\x98\x80")); // U+1F600
}

static void testJsonNested() {
    Json::Value v;
    std::string err;
    const char *text = "{\"a\":[1,2,{\"b\":\"c\"}],\"d\":{\"e\":null},\"f\":[]}";
    SY_CHECK(Json::parse(text, v, &err));

    SY_CHECK_EQ(v["a"].asArray().size(), 3u);
    SY_CHECK_EQ(v["a"].asArray()[2]["b"].asString(), std::string("c"));
    SY_CHECK(v["d"]["e"].isNull());
    SY_CHECK_EQ(v["f"].asArray().size(), 0u);

    // A missing key yields null rather than throwing or inserting.
    SY_CHECK(v["nope"].isNull());
    SY_CHECK(v["a"]["not-an-object"].isNull());

    // Re-serialising and re-parsing must be stable.
    Json::Value again;
    SY_CHECK(Json::parse(v.serialize(), again, &err));
    SY_CHECK_EQ(again["a"].asArray()[2]["b"].asString(), std::string("c"));
    Json::Value pretty;
    SY_CHECK(Json::parse(v.serialize(true), pretty, &err));
    SY_CHECK_EQ(pretty["a"].asArray().size(), 3u);
}

// Addresses exceed 2^53, so they must not be stored as JSON numbers.
static void testJsonLargeAddresses() {
    const uint64_t big = 0x0123456789ABCDEFULL;

    Json::Object o;
    o["address"] = Json::Value(std::string("0x123456789ABCDEF"));
    Json::Value parsed;
    std::string err;
    SY_CHECK(Json::parse(Json::Value(o).serialize(), parsed, &err));
    SY_CHECK_EQ(parsed["address"].asUInt64(), big);

    // Decimal strings work too.
    Json::Object o2;
    o2["n"] = Json::Value(std::string("18446744073709551615"));
    SY_CHECK(Json::parse(Json::Value(o2).serialize(), parsed, &err));
    SY_CHECK_EQ(parsed["n"].asUInt64(), 18446744073709551615ULL);

    // Garbage falls back rather than yielding a partial parse.
    Json::Object o3;
    o3["n"] = Json::Value(std::string("0xZZ"));
    SY_CHECK(Json::parse(Json::Value(o3).serialize(), parsed, &err));
    SY_CHECK_EQ(parsed["n"].asUInt64(7), 7ULL);
}

static void testSessionRoundTrip() {
    Session out;
    out.name = "test session";
    out.targetBundle = "com.example.game";

    Bookmark bookmark;
    bookmark.name = "player health";
    bookmark.address = 0x104BD8000ULL; // above 2^32
    bookmark.type = ValueType::Float32;
    bookmark.notes = "line1\nline2\twith\ttabs";
    bookmark.group = "combat";
    out.bookmarks.push_back(bookmark);

    FreezeEntry freeze{};
    freeze.address = 0x7FFFFFFFFFFFULL;
    freeze.type = ValueType::Int64;
    freeze.value = {1, 2, 3, 4, 5, 6, 7, 8};
    freeze.label = "gold";
    freeze.active = false;
    freeze.autoIncrement = true;
    freeze.incrementStep = -5;
    out.freezeEntries.push_back(freeze);

    Session::PatchRecord patch{};
    patch.address = 0x100001234ULL;
    patch.patchHex = "1F 20 03 D5";
    patch.originalHex = "C0 03 5F D6";
    patch.label = "nop the check";
    patch.autoApply = true;
    out.patches.push_back(patch);

    PointerChain chain{};
    chain.moduleName = "MyGame";
    chain.moduleOffset = 0xABCDEF;
    chain.offsets = {0x10, -0x20, 0x30};
    out.pointerChains.push_back(chain);

    out.searchHistory = {"100", "3.14", "quote\"inside", "with\nnewline"};

    const std::string path = tempPath("session.json");
    SY_CHECK(SessionManager::save(out, path));

    Session in;
    SY_CHECK(SessionManager::load(path, in));

    SY_CHECK_EQ(in.name, out.name);
    SY_CHECK_EQ(in.targetBundle, out.targetBundle);

    SY_CHECK_EQ(in.bookmarks.size(), 1u);
    if (in.bookmarks.size() == 1) {
        SY_CHECK_EQ(in.bookmarks[0].name, bookmark.name);
        SY_CHECK_EQ(in.bookmarks[0].address, bookmark.address);
        SY_CHECK_EQ(static_cast<int>(in.bookmarks[0].type), static_cast<int>(bookmark.type));
        SY_CHECK_EQ(in.bookmarks[0].notes, bookmark.notes);
        SY_CHECK_EQ(in.bookmarks[0].group, bookmark.group);
    }

    SY_CHECK_EQ(in.freezeEntries.size(), 1u);
    if (in.freezeEntries.size() == 1) {
        SY_CHECK_EQ(in.freezeEntries[0].address, freeze.address);
        SY_CHECK_EQ(static_cast<int>(in.freezeEntries[0].type), static_cast<int>(freeze.type));
        SY_CHECK(in.freezeEntries[0].value == freeze.value);
        SY_CHECK_EQ(in.freezeEntries[0].label, freeze.label);
        SY_CHECK_EQ(in.freezeEntries[0].active, freeze.active);
        SY_CHECK_EQ(in.freezeEntries[0].autoIncrement, freeze.autoIncrement);
        SY_CHECK_EQ(in.freezeEntries[0].incrementStep, freeze.incrementStep);
    }

    SY_CHECK_EQ(in.patches.size(), 1u);
    if (in.patches.size() == 1) {
        SY_CHECK_EQ(in.patches[0].address, patch.address);
        SY_CHECK_EQ(in.patches[0].patchHex, patch.patchHex);
        SY_CHECK_EQ(in.patches[0].originalHex, patch.originalHex);
        SY_CHECK_EQ(in.patches[0].label, patch.label);
        SY_CHECK_EQ(in.patches[0].autoApply, patch.autoApply);
    }

    SY_CHECK_EQ(in.pointerChains.size(), 1u);
    if (in.pointerChains.size() == 1) {
        SY_CHECK_EQ(in.pointerChains[0].moduleName, chain.moduleName);
        SY_CHECK_EQ(in.pointerChains[0].moduleOffset, chain.moduleOffset);
        SY_CHECK_EQ(in.pointerChains[0].offsets.size(), 3u);
        SY_CHECK(in.pointerChains[0].offsets == chain.offsets);
    }

    SY_CHECK_EQ(in.searchHistory.size(), 4u);
    SY_CHECK(in.searchHistory == out.searchHistory);

    std::remove(path.c_str());
}

static void testSessionEmptyRoundTrip() {
    Session out;
    out.name = "empty";
    const std::string path = tempPath("empty.json");
    SY_CHECK(SessionManager::save(out, path));

    Session in;
    SY_CHECK(SessionManager::load(path, in));
    SY_CHECK_EQ(in.name, std::string("empty"));
    SY_CHECK_EQ(in.bookmarks.size(), 0u);
    SY_CHECK_EQ(in.freezeEntries.size(), 0u);
    std::remove(path.c_str());
}

// Type tags are persisted as strings, so reordering the ValueType enum cannot
// silently reinterpret a saved session.
static void testSessionTypeTagsAreStable() {
    Session out;
    Bookmark b;
    b.address = 0x1000;
    b.type = ValueType::UInt16;
    out.bookmarks.push_back(b);

    const std::string path = tempPath("tags.json");
    SY_CHECK(SessionManager::save(out, path));

    // The file must contain the tag, not an enum ordinal.
    FILE *f = std::fopen(path.c_str(), "rb");
    SY_CHECK(f != nullptr);
    std::string contents;
    if (f) {
        char buf[4096];
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        contents = buf;
        std::fclose(f);
    }
    SY_CHECK(contents.find("uint16") != std::string::npos);

    Session in;
    SY_CHECK(SessionManager::load(path, in));
    SY_CHECK_EQ(in.bookmarks.size(), 1u);
    if (in.bookmarks.size() == 1)
        SY_CHECK_EQ(static_cast<int>(in.bookmarks[0].type), static_cast<int>(ValueType::UInt16));

    std::remove(path.c_str());
}

static void testSessionLoadFailures() {
    Session in;
    SY_CHECK(!SessionManager::load(tempPath("does-not-exist.json"), in));

    // A corrupt file must be refused, not partially applied.
    const std::string path = tempPath("corrupt.json");
    FILE *f = std::fopen(path.c_str(), "wb");
    SY_CHECK(f != nullptr);
    if (f) {
        std::fputs("{\"name\":\"broken\",", f);
        std::fclose(f);
    }
    Session target;
    target.name = "untouched";
    SY_CHECK(!SessionManager::load(path, target));
    SY_CHECK_EQ(target.name, std::string("untouched"));
    std::remove(path.c_str());
}

// save() must create missing parent directories; it previously wrote straight to
// the path and failed silently unless the directory happened to exist.
static void testSessionCreatesDirectories() {
    const std::string dir = tempPath("nested/deeper");
    const std::string path = dir + "/session.json";

    Session out;
    out.name = "nested";
    SY_CHECK(SessionManager::save(out, path));

    Session in;
    SY_CHECK(SessionManager::load(path, in));
    SY_CHECK_EQ(in.name, std::string("nested"));

    std::remove(path.c_str());
    ::rmdir(dir.c_str());
    ::rmdir(tempPath("nested").c_str());
}

static void run() {
    testJsonScalars();
    testJsonRejectsMalformed();
    testJsonEscapesRoundTrip();
    testJsonUnicodeEscapes();
    testJsonNested();
    testJsonLargeAddresses();
    testSessionRoundTrip();
    testSessionEmptyRoundTrip();
    testSessionTypeTagsAreStable();
    testSessionLoadFailures();
    testSessionCreatesDirectories();
}

SY_MAIN("test_session")
