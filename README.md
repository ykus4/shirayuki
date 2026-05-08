# shirayuki

KittyMemory-inspired memory search & patch toolkit for jailbroken iOS (Theos tweak).

Inject into any app, scan/modify memory in real-time through a floating overlay GUI.

---

## Features

| Category | Details |
|----------|---------|
| **Memory R/W** | Mach VM API, auto page-protection handling, typed read/write |
| **Pattern Scan** | IDA-style with `??` wildcards, string search, typed value scan |
| **Patch** | Apply/restore with original-byte backup, NOP sled (ARM64) |
| **Freeze** | Lock values at 60fps via background thread, per-entry toggle |
| **Pointer Scan** | Recursive chain finder (configurable depth/offset), ASLR-independent |
| **Overlay GUI** | Floating panel — dark theme, SF Symbols, 5 tabs, drag-to-move |

---

## GUI

Tap the floating **S** button to open the panel.

| Tab | Function |
|-----|----------|
| **Search** | Scan memory by int32 / float / hex pattern / string. Tap result to write or freeze. |
| **Patch** | Write hex bytes to address. Shows original for undo. |
| **Freeze** | Lock address to value. Tap to pause/resume, swipe to delete. |
| **Ptr** | Find pointer chains to target address. Validates at runtime. |
| **Dump** | Hex dump any address, copy to clipboard. |

---

## Project Structure

```
shirayuki/
├── Makefile                       — Theos build
├── Shirayuki.plist                — Substrate filter (target bundle)
├── ShirayukiMemory/
│   ├── ShirayukiMemory.hpp/cpp    — Core API (read/write/scan/patch)
│   ├── Freeze.hpp/cpp             — Value freeze manager
│   └── PointerScan.hpp/cpp        — Pointer chain scanner
├── GUI/
│   ├── SYTheme.h/m                — Colors, fonts, SF Symbols
│   ├── SYResultCell.h/m           — Card-style table cell
│   ├── ShirayukiWindow.h/m        — Floating UIWindow
│   └── ShirayukiViewController.h/m — Main panel (all tabs)
├── Tweak/
│   └── Tweak.xm                   — Entry point + UIWindow hook
└── layout/DEBIAN/control           — dpkg metadata
```

---

## Quick Start

### 1. Set target app

Edit `Shirayuki.plist`:
```
{ Filter = { Bundles = ( "com.example.targetapp" ); }; }
```

### 2. Build & install

```bash
make package install
```

### 3. Use

- Launch the target app
- Tap the floating **S** button
- Search, patch, freeze — all from the GUI

---

## API (for programmatic use in Tweak.xm)

```cpp
using namespace Shirayuki;

// Find a loaded framework
auto img = Image::find("UnityFramework");

// Pattern scan
auto hits = Scanner::findPatternInImage(img, "FF 43 01 D1 ?? ?? ?? ??");

// Patch
auto p = Patch::createNop(Image::absoluteAddress(img, 0x123456), 2);
p.apply();

// Value search + freeze
auto addrs = Scanner::findValue<int32_t>(region.start, region.size, 100);
FreezeManager::shared().addValue<int32_t>(addrs[0], 9999);
FreezeManager::shared().start();

// Pointer scan
PointerScanConfig cfg;
cfg.targetAddress = addrs[0];
cfg.maxDepth = 3;
auto chains = PointerScanner::scan(cfg);
```

---

## Requirements

- Jailbroken iOS 15.0+ (arm64)
- [Theos](https://theos.dev)
- Substrate or Substitute

---

## Disclaimer

For security research, CTF challenges, and educational use on devices you own.
