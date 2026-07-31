# Shirayuki

In-app memory toolkit overlay for jailbroken iOS. A Theos tweak that injects a
floating panel into any UIKit app for interactive memory searching, patching,
freezing, watching and pointer scanning.

**Platform** iOS 15.0+, arm64 · **Language** ObjC++ / C++17 · **Injection** ElleKit, Substrate or Substitute

## Install

Download the `.deb` from [Releases](https://github.com/ykus4/shirayuki/releases)
and install with a file manager or `dpkg -i`.

Packages are built **rootless**, which is what every iOS 15+ jailbreak
(palera1n, Dopamine, XinaA15) uses — a rooted `.deb` installs to paths that do not
exist there. For a legacy rooted jailbreak, build from source with
`make THEOS_PACKAGE_SCHEME= package`.

By default the tweak loads into **all UIKit applications**, including system apps.
See [Architecture → Injection filter](architecture.md#injection-filter) to narrow
that down.

## Where to go next

- **[Features](features.md)** — what each tab does, and which capabilities exist
  only as a C++ API so far
- **[Architecture](architecture.md)** — the layering rule that makes the test suite
  possible, and the invariants worth knowing before changing the core
- **[C++ API](api.md)** — driving `ShirayukiMemory/` from your own tweak
- **[Development](development.md)** — running the whole verification suite without
  Theos or a device
- **[Release](release.md)** — how versioning and the release workflow behave

## A note on what is verified

The C++ core has a host-native test suite (5 suites, 527 checks) that exercises
real Mach VM reads, writes, region walks, scans and session round-trips. CI also
compiles *and links* every ObjC/ObjC++ source against the real iPhoneOS SDK.

What CI cannot verify is runtime behaviour inside a host app — panel presentation,
gesture handling, or whether a scan finds what you expect in a particular game.
Treat those as needing manual checking on device.

!!! warning "Use on devices you own"
    Shirayuki reads and writes another process's memory. It is intended for
    security research, CTF challenges and education on hardware you control.
