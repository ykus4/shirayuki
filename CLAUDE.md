# CLAUDE.md — Shirayuki

## Project overview

Jailbroken iOS memory toolkit implemented as a Theos tweak (`.dylib` injected via Substrate/Substitute). ObjC++ codebase. Targets arm64, iOS 15+.

## Architecture

```
ShirayukiMemory/   — pure C++ library (no ObjC, no UIKit)
GUI/Handlers/      — ObjC++ tab handlers (mix of ObjC + C++)
GUI/               — ObjC UI layer (UIKit)
Tweak/Tweak.xm     — Logos entry point
```

### C++/ObjC boundary rule

**Critical**: The Theos/Clang build rejects C++ template syntax (`<Type>`) inside `.mm` files when used inside `dispatch_async` blocks — the parser misreads angle brackets as ObjC generic parameters.

All C++ template calls (`Scanner::findValue<T>`, `std::vector<RegionInfo>`, etc.) must stay in **pure `.cpp` files**. The bridge is `GUI/Handlers/SYScanHelper.cpp` — a C-linkage wrapper that exposes plain C functions callable from `.mm`.

Never put C++ STL templates inside `dispatch_async` blocks in `.mm` files.

### Block capture rule

`__weak typeof(self) weakSelf` / `__strong typeof(weakSelf) strongSelf` inside nested `dispatch_async` blocks is **rejected by this Theos/Clang configuration**. Use direct `self` capture instead. This is the established pattern throughout all handler files.

## Build

```bash
# Requires Theos installed at $THEOS
make clean && make          # debug build
make package                # produces packages/*.deb
make package install        # install to THEOS_DEVICE_IP
```

Build runs on GitHub Actions (macOS runner) — see `.github/workflows/build.yml`.

## Key files

| File | Role |
|---|---|
| `ShirayukiMemory/ShirayukiMemory.hpp/cpp` | Mach VM read/write, region scan, pattern scan, value scan, ARM64 disasm, image/symbol lookup |
| `ShirayukiMemory/Freeze.hpp/cpp` | FreezeManager: periodic writer, auto-increment, conditional triggers |
| `ShirayukiMemory/Watchpoint.hpp/cpp` | WatchManager: polling monitor, change detection, callbacks |
| `ShirayukiMemory/PointerScan.hpp/cpp` | Recursive pointer chain finder, chain validation |
| `ShirayukiMemory/Session.hpp/mm` | JSON save/load for bookmarks, patches, freezes, pointer chains |
| `GUI/Handlers/SYScanHelper.cpp` | C-linkage wrapper: `SYScanAll`, `SYScanRegion`, `SYMemRead` |
| `GUI/Handlers/SYSearchHandler.mm` | Search tab: scan, narrow (changed/unchanged/increased/decreased/exact), batch modify, export JSON |
| `GUI/Handlers/SYPatchHandler.mm` | Patch tab: hex patch, undo/redo stack |
| `GUI/Handlers/SYFreezeHandler.mm` | Freeze tab: lock values, auto-increment toggle |
| `GUI/Handlers/SYWatchHandler.mm` | Watch tab: real-time monitor, prev→current diff |
| `GUI/Handlers/SYPointerHandler.mm` | Pointer tab: chain scan, validate, copy |
| `GUI/Handlers/SYDumpHandler.mm` | Dump tab: hex dump, ARM64 disassembly, NOP |
| `GUI/ShirayukiViewController.mm` | Main panel: tab routing, input field, long-press menus, session auto-save |
| `GUI/SYValueTypeUtil.h` | Type string ↔ ValueType conversion, parse/format value bytes |

## Adding a new tab

1. Create `GUI/Handlers/SYFooHandler.h` and `.mm` implementing `SYTabHandler` protocol
2. Add to `Shirayuki_FILES` in `Makefile`
3. Instantiate in `ShirayukiViewController.mm` `viewDidLoad` alongside existing handlers

## Versioning & release

- Dev builds: version auto-computed as `BASE-dev.N+sha` in CI
- Release: `git tag v0.x.0 && git push origin v0.x.0` — triggers release workflow
- Version source of truth: `layout/DEBIAN/control` `Version:` field

## Code style

- `clang-format` enforced in CI (`make fmt-check`)
- Run `make fmt` before committing
- C++17, `-fobjc-arc`
- No `__weak`/`__strong` in nested dispatch blocks (see block capture rule above)
