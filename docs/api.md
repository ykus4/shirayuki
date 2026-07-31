# C++ API

`ShirayukiMemory/` is usable on its own — include the umbrella header and link the
core sources into your own tweak.

```cpp
#include "ShirayukiMemory.hpp"
using namespace Shirayuki;
```

Every function is in namespace `Shirayuki`. Nothing here needs Foundation.

## Reading and writing

```cpp
// Raw
uint8_t buf[16];
if (Memory::read(addr, buf, sizeof(buf)) != Status::Success) { /* handle */ }
Memory::write(addr, buf, sizeof(buf));

// Typed, reporting failure
float hp = 0;
if (Memory::readValue<float>(addr, hp) == Status::Success) { /* use hp */ }

// Typed, optional
if (auto hp = Memory::tryReadValue<float>(addr)) { /* use *hp */ }

Memory::writeValue<float>(addr, 99999.0f);
```

`Memory::write` handles page protection itself: it tries a direct write, and on
failure re-protects the page, writes, restores the original protection and
invalidates the instruction cache.

!!! warning "Always check the status"
    A failed read leaves the buffer untouched. Treating a zeroed buffer as a real
    value is how a failed read becomes a zero write.

## Regions

```cpp
auto all   = Memory::listRegions();
auto rw    = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
auto heap  = Memory::listRegionsFiltered(RegionFilter::HeapOnly);
auto data  = Memory::listRegionsFiltered(RegionFilter::DataOnly);
auto stack = Memory::listRegionsFiltered(RegionFilter::StackOnly);
auto code  = Memory::listRegionsFiltered(RegionFilter::Executable);

RegionInfo r = Memory::getRegionInfo(addr);
// r.start, r.size, r.protection, r.userTag, r.label ("MALLOC", "__DATA", …)
// r.isReadable() / isWritable() / isExecutable() / isHeap() / isStack()
```

`HeapOnly` matches the kernel's malloc allocator tags; `DataOnly` matches real
Mach-O `__DATA`/`__DATA_CONST`/`__DATA_DIRTY` segment bounds. An address-range
heuristic cannot work on arm64.

## Images and symbols

```cpp
ImageInfo img = Image::find("UnityFramework");   // substring match on the path
if (img.isValid()) {
    uintptr_t fn = Image::absoluteAddress(img, 0x123456);  // ASLR-independent
}

for (const auto &i : Image::listAll()) { /* i.name, i.base, i.slide */ }

uintptr_t sym = Image::findSymbol("libSystem.B.dylib", "malloc");  // 0 if absent
```

## Scanning

All scans read chunked copies through `Memory::read`. Nothing dereferences target
memory directly, so a concurrent `free()` in the host app cannot fault the process.

```cpp
// Typed value — strides by the value width, so only aligned matches
auto hits = Scanner::findValue<float>(region.start, region.size, 99.0f);

// Byte-level, with an explicit stride (1 = unaligned matches too)
uint8_t needle[4] = {0x44, 0x33, 0x22, 0x11};
auto any = Scanner::findValueBytes(start, len, needle, 4, /*stride=*/1);

// IDA-style pattern, ?? for wildcards
auto pat   = Scanner::findPattern(start, len, "FF 43 01 D1 ?? ?? ??");
uintptr_t first = Scanner::findPatternFirst(start, len, "FF 43 01 D1");
auto inImg = Scanner::findPatternInImage(img, "FF 43 01 D1 ?? ?? ??");

// Strings
auto strs = Scanner::findString(start, len, "PlayerName");
auto rex  = Scanner::findRegex(start, len, "HP:[0-9]+");

// Fuzzy numeric — within ±tolerance
auto fuzzy = Scanner::findValueFuzzy(start, len, 99.0, 0.5, ValueType::Float32);
```

## Narrowing

```cpp
std::vector<Scanner::Candidate> candidates;
for (uintptr_t a : hits) {
    Scanner::Candidate c;
    c.address = a;
    c.snapshotValue.resize(valueTypeSize(ValueType::Int32));
    Memory::read(a, c.snapshotValue.data(), c.snapshotValue.size());
    candidates.push_back(std::move(c));
}

// … let the app change the value …

auto changed = Scanner::narrowResults(candidates, ValueType::Int32,
                                      CompareMode::Changed);

uint8_t target[kMaxValueSize] = {};
ValueFormat::parse("42", ValueType::Int32, target);
auto exact = Scanner::narrowResults(candidates, ValueType::Int32,
                                    CompareMode::Exact, target);
```

Modes: `Exact` `Changed` `Unchanged` `Increased` `Decreased` `GreaterThan`
`LessThan`. The last three need `compareValue`; the rest ignore it.

For an unknown initial value, seed every aligned slot and narrow by behaviour:

```cpp
auto seeded = Scanner::seedUnknownCandidates(start, len, ValueType::Int32,
                                             kMaxScanResults);
```

## Value types

```cpp
size_t      w   = valueTypeSize(ValueType::Int64);      // 8
std::string s   = valueTypeLabel(ValueType::Int64);     // "i64"  (short)
std::string tag = ValueFormat::toTag(ValueType::Int64); // "int64" (canonical)

ValueType t = ValueFormat::fromTag("i64");   // accepts short and canonical
ValueType out;
if (!ValueFormat::tryFromTag(userInput, out)) { /* report a bad tag */ }

// Parse: returns bytes written, 0 on rejection. Never throws.
uint8_t buf[kMaxValueSize] = {};
if (ValueFormat::parse("-42", ValueType::Int32, buf) == 0) { /* invalid */ }

ValueFormat::format(buf, ValueType::Int32);         // "-42"        re-parseable
ValueFormat::formatDisplay(buf, ValueType::Int32);  // "-42 (0xFFFFFFD6)"  display

// Numeric three-way compare. Never use memcmp for this.
int cmp = compareTypedBytes(a, b, ValueType::Float32);
```

`parse` rejects empty input, trailing characters and out-of-range values, and
accepts an optional sign plus a `0x` prefix.

## Patching

```cpp
Patch p = Patch::createWithHex(addr, "1F 20 03 D5");
if (p.isValid() && p.apply()) {
    p.originalHex();   // bytes before the patch
    p.patchHex();      // bytes written
    p.isApplied();
    p.restore();       // put the originals back
}

Patch::createNop(addr, 2).apply();   // two ARM64 NOPs
```

`createWithHex` snapshots the original bytes, so it must succeed before anything is
written. Keep the `Patch` object if you want to restore later — discarding it loses
the originals.

## Freeze

```cpp
auto &fm = FreezeManager::shared();

uint64_t id = fm.addValue<float>(addr, 99999.0f, "hp");
fm.start(kFreezeIntervalMs);          // 16 ms
fm.setAutoIncrement(id, true, 1);     // +1 each tick instead of a constant
fm.setActive(id, false);              // pause, keep tracking
fm.remove(id);
fm.stop();

// Write only while a threshold condition holds
float clampTo = 50.0f, threshold = 100.0f;
fm.addConditional(addr, &clampTo, sizeof(clampTo), ValueType::Float32,
                  CompareMode::GreaterThan, &threshold, sizeof(threshold),
                  [](uint64_t id, uintptr_t a) { /* fired, outside the lock */ });
```

## Watch

```cpp
auto &wm = WatchManager::shared();

uint64_t id = wm.add(addr, ValueType::Float32, "hp");
wm.setCallback([](const WatchEntry &e) {
    // e.address, e.previousValue, e.currentValue, e.changeCount, e.hasChanged
});
wm.start(kWatchIntervalMs);           // 100 ms

int32_t limit = 10;
wm.setTrigger(id, CompareMode::LessThan, &limit, sizeof(limit),
              /*oneShot=*/true, [](const WatchEntry &e) { /* … */ });
```

`entries()` returns a copy. Callbacks fire outside the manager lock, so it is safe
to call back into it — but do not assume the entry still exists.

## Pointer chains

```cpp
PointerScanConfig cfg;
cfg.targetAddress = addr;
cfg.maxDepth      = kPointerScanDefaultDepth;
cfg.maxOffset     = 0x1000;
cfg.maxResults    = 100;

for (const auto &chain : PointerScanner::scan(cfg)) {
    chain.toString();                 // "[GameLib + 0xABCD00] → +0x08 → +0x00"
    uintptr_t now = chain.resolve();  // 0 if it no longer resolves
    PointerScanner::validate(chain, addr);
}
```

Chains are module-relative, so they survive relaunches and ASLR.

## Disassembly and hex

```cpp
for (const auto &insn : Disasm::disassemble(addr, 16)) {
    Disasm::formatInstruction(insn);   // "STP x29, x30, [sp, #-0x10]!"
}

Hex::toBytes("DE AD BE EF");             // {0xDE, 0xAD, 0xBE, 0xEF}
Hex::fromBytes(bytes);                   // "DE AD BE EF"
Hex::isValid("DE AD");                   // true
Hex::dump(addr, 64);                     // annotated dump, bounded by
                                         // kMaxHexDumpLength
```

## Sessions

```cpp
Session s;
s.name         = "my session";
s.targetBundle = "com.example.game";

Bookmark b{ "player hp", addr, ValueType::Float32, "notes", "combat" };
s.bookmarks.push_back(b);
s.freezeEntries = FreezeManager::shared().entries();

SessionManager::save(s, SessionManager::autoSavePath(s.targetBundle));

Session loaded;
if (SessionManager::load(path, loaded)) { /* … */ }

SessionManager::listSessions();
SessionManager::deleteSession(path);
```

Addresses persist as hex strings and types as tags, so values above 2^53 and a
future reordering of `ValueType` both survive a round trip. `save` writes via a temp
file and rename.

## Other modules

- **`GroupScan`** — match a multi-field struct in one pass, e.g. `{HP, MP, state}`.
- **`Snapshot`** — capture a range and diff two captures at byte level.
- **`ReadCache`** — per-thread TTL cache, for polls tighter than the cache lifetime.
- **`ThreadList`** — enumerate threads with ARM64 PC/SP/LR and run state.
- **`Speedhack`** — API surface only; the `mach_absolute_time` interposition is
  unimplemented.

## Configuration

All limits live in `ShirayukiConfig.hpp`: `kMaxScanResults`, `kMaxRegionSize`,
`kScanChunkSize`, `kMaxValueSize`, `kMaxHexDumpLength`, `kFreezeIntervalMs`,
`kWatchIntervalMs`, the pointer-scan defaults and the worker interval clamps.
