# ❄️ Shirayuki

<p align="center">
  <img src="assets/icon.png" width="180" alt="shirayuki icon" />
</p>

<p align="center">
  <b>In-app memory toolkit overlay for jailbroken iOS</b>
</p>

<p align="center">
  <img src="https://github.com/ykus4/shirayuki/actions/workflows/build.yml/badge.svg" alt="Build" />
  <img src="https://github.com/ykus4/shirayuki/actions/workflows/format.yml/badge.svg" alt="Format Check" />
  <img src="https://img.shields.io/badge/platform-iOS%2015%2B-blue?style=flat" />
  <img src="https://img.shields.io/badge/arch-arm64-lightgrey?style=flat" />
  <img src="https://img.shields.io/badge/language-ObjC%2B%2B-orange?style=flat" />
</p>

---

## What is this?

Shirayuki injects a **floating overlay panel** into any app on a jailbroken iPhone. No respring required per session — just tap the snowflake button and start hacking.

```
┌─────────────────────────────────────────────┐
│  Target App                                 │
│                                             │
│           ┌──────────────────────┐          │
│           │  ❄️  Shirayuki Panel  │          │
│           │  ┌──┬──┬──┬──┬──┬──┐│          │
│           │  │🔍│🔧│🔒│👁│🌿│💾││          │
│           │  └──┴──┴──┴──┴──┴──┘│          │
│           │  [Search tab shown]  │          │
│           │  > int32  [ Scan ]   │          │
│           │  0x1A2B3C4D  = 100   │          │
│           │  0x1A2B3C50  = 100   │          │
│           └──────────────────────┘          │
│                              ❄️ ←drag       │
└─────────────────────────────────────────────┘
```

---

## Features

### 🔍 Search & Narrow

```
First scan         →  Narrow: Changed     →  Narrow: Exact 42
──────────────        ──────────────────     ────────────────
2000 results          87 results             3 results
0x1A001234            0x1A001234             0x1A001234  ✓
0x1A001238            0x1A001238             0x1A005580  ✓
0x1A001240            ...                    0x1B000020  ✓
...
```

| Type | Input example |
|---|---|
| `int32` `int16` `int64` | `100` |
| `float` `double` | `3.14` |
| `hex` (IDA pattern) | `FF 43 01 D1 ?? ?? ??` |
| `string` | `PlayerName` |
| `regex` | `HP:[0-9]+` |

### 🔧 Patch + Undo/Redo

```
Address      Original       Patched        State
──────────── ────────────── ────────────── ──────
0x1A001234   FF 43 01 D1    1F 20 03 D5    ✅ ON
0x1A005580   E0 03 00 AA    1F 20 03 D5    ⏸ OFF
             ↑ auto backup                 ↑ toggle

[Undo] → restore last patch
[Redo] → reapply
```

### 🔒 Freeze + Auto-Increment

```
0x1A001234  =  99999  (float)  [FROZEN]   ← tap to pause
0x1A001238  =  1      (int32)  [INC +1]   ← auto-increment each tick
0x1A00123C  =  100    (int32)  [PAUSED]   ← frozen but inactive
```

### 👁 Watch (real-time diff)

```
0x1A001234   float    42.0 → 43.0   ▲  (changed 7x)
0x1A001238   int32    99  → 99      ·  (unchanged)
0x1A00123C   int32    0   → 255     ▲  (changed 1x)
```

### 🌿 Pointer Scan

```
Target: 0x1A001234

Chain 1: [UnityFramework + 0x1234AB] → +0x10 → +0x28 → +0x00  ✓
Chain 2: [GameLib + 0xABCD00] → +0x08 → +0x00                  ✓
Chain 3: [GameLib + 0xABCD10] → +0x08 → +0x00                  ??
```

### 💾 Hex Dump & Disassembly

```
0xADDR len      →  hex dump
0xADDR asm      →  ARM64 disassembly

0x1A001234  FF 43 01 D1   STP  x29, x30, [sp, #-0x10]!
0x1A001238  FD 03 00 91   MOV  x29, sp
0x1A00123C  1F 20 03 D5   NOP                           ← long-press to NOP
```

---

## Quick Start

```bash
# Set device IP
export THEOS_DEVICE_IP=192.168.x.x

# Build + install + respring
make package install
```

To restrict which app Shirayuki injects into, edit `Shirayuki.plist`:

```xml
<key>Bundles</key>
<array>
  <string>com.example.targetapp</string>
</array>
```

---

## Programmatic API

```cpp
using namespace Shirayuki;

// Pattern scan
auto img  = Image::find("UnityFramework");
auto hits = Scanner::findPatternInImage(img, "FF 43 01 D1 ?? ?? ??");

// Patch (NOP 2 instructions)
Patch::createNop(Image::absoluteAddress(img, 0x123456), 2).apply();

// Value search
auto results = Scanner::findValue<float>(region.start, region.size, 99.0f);

// Freeze
FreezeManager::shared().addValue<float>(addr, 99999.0f);
FreezeManager::shared().start(16); // 16ms tick

// Watch
WatchManager::shared().add(addr, ValueType::Float32);
WatchManager::shared().setCallback([](const WatchEntry &e) {
    // e.previousValue, e.currentValue, e.changeCount
});

// Pointer scan
PointerScanConfig cfg{ .targetAddress = addr, .maxDepth = 3 };
auto chains = PointerScanner::scan(cfg);

// Session
SessionManager::save(session, SessionManager::autoSavePath("com.example.app"));
```

---

## Project Structure

```
shirayuki/
├── Tweak/Tweak.xm                     ← injection entry (Logos)
├── ShirayukiMemory/
│   ├── ShirayukiMemory.hpp/cpp        ← Mach VM, scan, patch, disasm
│   ├── Freeze.hpp/cpp                 ← value locker + auto-increment
│   ├── Watchpoint.hpp/cpp             ← polling monitor
│   ├── PointerScan.hpp/cpp            ← chain finder
│   └── Session.hpp/mm                 ← JSON persistence
└── GUI/
    ├── ShirayukiViewController.mm     ← main panel + tab routing
    ├── SYTheme / SYToast / SYDragButton
    └── Handlers/
        ├── SYScanHelper.cpp           ← C++ isolation layer
        ├── SYSearchHandler.mm         ← search + narrow + batch
        ├── SYPatchHandler.mm          ← patch + undo/redo
        ├── SYFreezeHandler.mm         ← freeze + auto-increment
        ├── SYWatchHandler.mm          ← watchpoints
        ├── SYPointerHandler.mm        ← pointer chains
        └── SYDumpHandler.mm           ← hex dump + disassembly
```

---

## CI / Release

| Workflow | Trigger | Result |
|---|---|---|
| **Build** | push / PR → `main`, `dev` | artifact `.deb` + PR comment |
| **Format** | push / PR | clang-format check |
| **Release** | `git tag v*` | GitHub Release with `.deb` |

```bash
# Cut a release
git tag v0.2.0 && git push origin v0.2.0
```

Dev builds are versioned as `0.1.0-dev.N+sha7` automatically.

---

## Requirements

- Jailbroken iOS 15.0+ arm64
- [Theos](https://theos.dev)
- Substrate or Substitute

---

> For security research, CTF challenges, and educational use on devices you own.
