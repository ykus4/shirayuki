# CLAUDE.md — Shirayuki

## Project overview

Jailbroken iOS memory toolkit implemented as a Theos tweak (`.dylib` injected via Substrate/Substitute/ElleKit). ObjC++ codebase. Targets arm64, iOS 15+.

## Architecture

```
ShirayukiMemory/   — pure C++ library (no ObjC, no UIKit)
GUI/Handlers/      — ObjC++ tab handlers (mix of ObjC + C++)
GUI/               — ObjC UI layer (UIKit)
Tweak/Tweak.xm     — Logos entry point
tests/             — host-native C++ test suite (see Verification)
scripts/           — checks that run without Theos
```

`ShirayukiMemory/` must stay free of ObjC. It is what makes the test suite
possible, and the rule has been broken before: session persistence used to live in
a `Session.mm` that pulled in `NSJSONSerialization`.

## Verification

Theos is **not** needed for any of this, so it works on any machine with Xcode.
Run it before committing.

```bash
make test           # build + run the C++ core suite natively (5 suites, 527 checks)
make syntax-check   # compile AND link every ObjC/ObjC++/C++ source against the iPhoneOS SDK
make check          # fmt-check + test + syntax-check
```

`make test` works because the core is pure C++ and the Mach VM APIs it uses
(`vm_read_overwrite`, `vm_region_recurse_64`, `_dyld_*`) all exist on macOS — the
tests exercise real reads, writes, region walks and patches against the test
process's own memory. This is the project's only behavioural coverage; a Theos
build only proves things compile.

`make syntax-check` has two phases. The second one **links**, which matters: a C
function declared without `extern "C"` and called from a `.mm` file type-checks
fine in both translation units and fails only at link time. That reached CI once.
Only `Tweak/Tweak.xm` is excluded, since it needs the Logos preprocessor.

Do not edit a test to make it pass — the tests encode intended behaviour, and
several of them assert that a previous implementation was wrong.

## Build

```bash
# Requires Theos installed at $THEOS
make clean && make          # debug build
make package                # produces packages/*.deb
make package install        # install to THEOS_DEVICE_IP
```

Packages are built rootless (`THEOS_PACKAGE_SCHEME = rootless`) — required for
iOS 15+ jailbreaks (palera1n, Dopamine, XinaA15), where a rooted `.deb` installs
to paths that do not exist. Pass `make THEOS_PACKAGE_SCHEME=` for a legacy rooted
package. `Depends` accepts `ellekit | mobilesubstrate`.

Without `$THEOS`, the Makefile still provides the verification targets above and
gives a clear error for `make`/`package`/`install`.

Build and tests run on GitHub Actions — see `.github/workflows/`.

## The C++ standard flag

`Shirayuki_CCFLAGS` must use **`-std=gnu++17`**, not `-std=c++17`.

`typeof` is a GNU extension. Plain `-std=c++17` disables it, and every ARC
weak-self idiom (`__weak typeof(self) weakSelf = self;`) then fails with
`error: expected unqualified-id`. If the flag ever has to be plain `-std=c++17`,
use `__typeof__` (double underscore, always available).

**Use weak-self in escaping blocks.** Capturing `self` directly in a repeating
`NSTimer` block or any retained callback creates a retain cycle — that is how
`SYWatchHandler`'s 500 ms refresh timer ended up running for the life of the
process with `dealloc` never called.

### Two constraints this file used to state, both false

Earlier revisions claimed the build rejected C++ templates inside `dispatch_async`
blocks in `.mm` files, and that `__weak typeof(self)` was rejected in *nested*
dispatch blocks. Both were verified false against Apple clang. Templates in a
`.mm` dispatch block compile fine, and the weak-self failure has nothing to do
with nesting — it fails in a single non-nested block too, because of the `-std`
flag above. Do not reintroduce workarounds for either.

## Key files

| File | Role |
|---|---|
| `ShirayukiMemory/ShirayukiMemory.hpp` | Umbrella header. Every core `.cpp` and most GUI files include only this |
| `ShirayukiMemory/ShirayukiConfig.hpp` | All tunables and limits. New magic numbers belong here |
| `ShirayukiMemory/Memory.cpp` | Mach VM read/write, region enumeration, region filters |
| `ShirayukiMemory/Scanner.cpp` | Value / pattern / string / regex / fuzzy scans, result narrowing. Reads chunked copies via `Memory::read` — never dereference target memory directly |
| `ShirayukiMemory/ValueFormat.cpp` | Type metadata, tag vocabulary, parse, format, `compareTypedBytes`. Add a type here, not in a new switch |
| `ShirayukiMemory/Image.cpp` | Image base / slide lookup, symbol resolution |
| `ShirayukiMemory/Patch.cpp`, `Hex.cpp`, `Disasm.cpp` | Byte patching, hex conversion/dump, ARM64 disassembly |
| `ShirayukiMemory/Freeze.cpp` | FreezeManager: periodic writer, auto-increment, conditional triggers |
| `ShirayukiMemory/Watchpoint.cpp` | WatchManager: polling monitor, change detection, conditional triggers |
| `ShirayukiMemory/PointerScan.cpp` | Recursive pointer chain finder, chain validation |
| `ShirayukiMemory/GroupScan.cpp` | Multi-field struct matching (anchor sweep + verify) |
| `ShirayukiMemory/Snapshot.cpp` | Range capture and byte-level diff |
| `ShirayukiMemory/ReadCache.cpp` | Per-thread TTL read cache for the poll loops |
| `ShirayukiMemory/ThreadList.cpp`, `Speedhack.cpp` | Thread enumeration; speedhack (skeleton, needs on-device work) |
| `ShirayukiMemory/Json.cpp` | Minimal JSON reader/writer, so the core needs no Foundation |
| `ShirayukiMemory/Session.cpp` | Save/load for bookmarks, patches, freezes, chains. Addresses persist as hex strings and types as tags, so neither exceeding 2^53 nor reordering the enum corrupts a session |
| `GUI/SYTabHandler.h` | Protocol every tab implements |
| `GUI/SYInput.h/.m` | `SYFormatAddress` and `SYCommand` — the one address parser. Free functions are `extern "C"` so `.mm` callers link |
| `GUI/SYValueTypeUtil.h` | ObjC++ adapters over `ValueFormat`. Must not reimplement sizes or tag lookups |
| `GUI/Handlers/SYScanHelper.cpp` | Scan/narrow entry points for the search tab |
| `GUI/Handlers/SYDispatchUtil.h` | `SYAsync` — the background → main-queue sandwich |
| `GUI/ShirayukiViewController.mm` | Main panel: tab routing, input field, long-press menus, session auto-save |

## Conventions worth keeping

- **`ValueFormat::format` is plain and round-trippable; `formatDisplay` is annotated and display-only.** Use `format` for edit fields, session files and JSON export; `formatDisplay` for cell text. They are not interchangeable.
- **Never compare typed values with `memcmp`.** Use `compareTypedBytes`. A bytewise compare is wrong on little-endian ARM64 for every multi-byte type and for all signed and float values — this bug has been introduced twice, in search narrowing and in conditional freeze.
- **Check `Memory::read`/`write` status.** A failed read that returns a zeroed buffer is indistinguishable from a real 0, and displaying that 0 in an editable field turns a failed read into a real zero write. `readValue` returns `Status`; `tryReadValue` returns `std::optional`.
- **Check `ValueFormat::parse`'s return.** 0 means the input was rejected; writing the zeroed buffer anyway is how a typo becomes a wrong memory write.
- **Bound anything sized from user input** before allocating (see `kMaxHexDumpLength`).

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

## Adding a new tab

1. Create `GUI/Handlers/SYFooHandler.h` and `.mm` implementing `SYTabHandler`
2. Add to `Shirayuki_FILES` in `Makefile`
3. Instantiate in `ShirayukiViewController.mm` `setupHandlers` alongside the others

## Versioning & release

- Dev builds: version auto-computed as `BASE-dev.N+sha` in CI
- Release: `git tag v0.x.0 && git push origin v0.x.0` — **merging to main does not release anything**; the tag push is the only trigger
- **The tag is the source of truth.** `release.yml` rewrites `layout/DEBIAN/control`'s `Version:` from the tag name before building, so the committed value only affects local and dev builds. Keep it in step with the latest tag anyway.
- A failed Theos build fails the workflow before the release is published, so a bad tag yields no release — but the tag still exists and has to be deleted.

## Code style

- `clang-format` enforced in CI (`make fmt-check`)
- Run `make fmt` before committing
- C++17 via `-std=gnu++17`, `-fobjc-arc`
- Use `__weak`/`__strong` self in escaping blocks (see the standard-flag section)
