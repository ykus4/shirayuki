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

### C++/ObjC boundary

C++ templates inside `dispatch_async` blocks in `.mm` files compile fine. Earlier revisions of this document claimed the parser rejected them; that was verified false against Apple clang (`-x objective-c++ -fobjc-arc -std=gnu++17`). There is no parser limitation to work around, and new code may use C++ from `.mm` freely.

`GUI/Handlers/SYScanHelper.hpp` was originally an `extern "C"` API handing back malloc'd arrays because of that belief. It is now an ordinary C++ interface (`namespace SYScan`), which also removed a class of bug the C boundary caused: the value width crossed it as a bare `size_t` that the caller read into a fixed `unsigned char[8]`, so searching for a string longer than 8 characters overflowed the caller's stack.

### `typeof` and the C++ standard flag

`Shirayuki_CCFLAGS` must use **`-std=gnu++17`**, not `-std=c++17`.

`typeof` is a GNU extension. Plain `-std=c++17` disables it, and every ARC weak-self idiom (`__weak typeof(self) weakSelf = self;`) then fails with `error: expected unqualified-id`. This has nothing to do with nested blocks — it fails in a single non-nested block too. If the flag ever has to be plain `-std=c++17`, use `__typeof__` (double underscore, always available).

**Do use weak-self in escaping blocks.** Capturing `self` directly in a repeating `NSTimer` block or any retained callback creates a retain cycle — this is how `SYWatchHandler`'s 500 ms refresh timer ended up living forever with `dealloc` never running.

## Build

```bash
# Requires Theos installed at $THEOS
make clean && make          # debug build
make package                # produces packages/*.deb
make package install        # install to THEOS_DEVICE_IP
```

Packages are built rootless (`THEOS_PACKAGE_SCHEME = rootless`) — required for
iOS 15+ jailbreaks (palera1n, Dopamine, XinaA15). Pass
`make THEOS_PACKAGE_SCHEME=` for a legacy rooted package.

## Injection filter

`Shirayuki.plist` filters on `com.apple.UIKit`, so the tweak loads into every
UIKit application and the panel is available in any app without reinstalling per
target. This does include system apps.

Keep the file comment-free — it is parsed as an old-style plist by the injection
runtime, and a parse failure means the tweak silently never loads. To restrict it
to one app, replace the bundle list:

```
{ Filter = { Bundles = ( "com.your.game" ); }; }
```

Build runs on GitHub Actions (macOS runner) — see `.github/workflows/build.yml`.

## Verification without a device

Theos is **not** needed for these, so they work on any machine with Xcode:

```bash
make test           # build + run the C++ core test suite natively
make syntax-check   # type-check GUI .mm/.m against the real iPhoneOS SDK
make check          # fmt-check + test + syntax-check
```

`make test` works because the core is pure C++ and the Mach VM APIs it uses
(`vm_read_overwrite`, `vm_region_recurse_64`, `_dyld_*`) all exist on macOS — the
tests exercise real reads and writes against the test process's own memory. This
is the project's only behavioural coverage; the Theos build only proves things
compile. Run it before any commit touching `ShirayukiMemory/`.

`make syntax-check` covers every `.mm`/`.m` file. Only `Tweak/Tweak.xm` is
excluded, since it needs the Logos preprocessor.

## Key files

| File | Role |
|---|---|
| `ShirayukiMemory/ShirayukiMemory.hpp/cpp` | Mach VM read/write, region scan, pattern scan, value scan, ARM64 disasm, image/symbol lookup |
| `ShirayukiMemory/ValueType.cpp` | Single descriptor table driving every type-dependent operation: size, tags, parse, format, compare. Add a type here, not in a switch |
| `ShirayukiMemory/ShirayukiConfig.hpp` | All tunables and limits. New magic numbers belong here |
| `ShirayukiMemory/Freeze.hpp/cpp` | FreezeManager: periodic writer, auto-increment, conditional triggers |
| `ShirayukiMemory/Watchpoint.hpp/cpp` | WatchManager: polling monitor, change detection, callbacks |
| `ShirayukiMemory/PointerScan.hpp/cpp` | Recursive pointer chain finder, chain validation |
| `ShirayukiMemory/Session.hpp/cpp` | Save/load for bookmarks, patches, freezes, pointer chains. Pure C++ — addresses persist as hex strings, types as tags, so neither exceeding 2^53 nor reordering the enum corrupts a saved session |
| `ShirayukiMemory/Json.cpp` | Minimal JSON reader/writer. Exists so the core needs no Foundation |
| `GUI/Handlers/SYScanHelper.hpp/cpp` | `SYScan`: scan, narrow and batch-write for the search tab. Captures value snapshots itself, so no caller has to size a buffer |
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
- C++17 via `-std=gnu++17`, `-fobjc-arc`
- Use `__weak`/`__strong` self in escaping blocks (see the `typeof` section above)
