# ❄️ Shirayuki

<p align="center">
  <img src="assets/icon.png" width="360" alt="shirayuki icon" />
</p>

<p align="center">
  <b>In-app memory toolkit overlay for jailbroken iOS</b>
</p>

<p align="center">
  <img src="https://github.com/ykus4/shirayuki/actions/workflows/build.yml/badge.svg" alt="Build" />
  <img src="https://github.com/ykus4/shirayuki/actions/workflows/test.yml/badge.svg" alt="Tests" />
  <img src="https://github.com/ykus4/shirayuki/actions/workflows/format.yml/badge.svg" alt="Format Check" />
  <img src="https://img.shields.io/badge/platform-iOS%2015%2B-blue?style=flat" />
  <img src="https://img.shields.io/badge/arch-arm64-lightgrey?style=flat" />
  <img src="https://img.shields.io/badge/language-ObjC%2B%2B-orange?style=flat" />
</p>

---

Shirayuki injects a **floating overlay panel** into any app on a jailbroken iPhone.
Tap the snowflake button and start hacking — no respring per session.

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

**Tabs:** Search · Patch · Freeze · Watch · Pointer · Dump · Threads · Modules

## Install

Grab the `.deb` from [Releases](https://github.com/ykus4/shirayuki/releases) and
install it with a file manager or `dpkg -i`. Requires a jailbroken **iOS 15.0+
arm64** device with **ElleKit** (rootless) or **Substrate/Substitute** (rooted).

Packages are built rootless, which is what palera1n, Dopamine and XinaA15 use.

## Build

```bash
export THEOS_DEVICE_IP=192.168.x.x
make package install
```

Requires [Theos](https://theos.dev).

## Develop

Theos is **not** needed for these — any machine with Xcode will do:

```bash
make test           # run the C++ core suite natively (5 suites, 527 checks)
make syntax-check   # compile and link every source against the iPhoneOS SDK
make check          # fmt-check + test + syntax-check
```

## Documentation

📖 **<https://ykus4.github.io/shirayuki>**

| | |
|---|---|
| [Install](docs/install.md) | Rooted vs rootless, verifying it loaded, troubleshooting |
| [Usage](docs/usage.md) | Finding a value, freezing it, patching, pointer chains |
| [Features](docs/features.md) | What each tab does, and what is core-API-only |
| [Architecture](docs/architecture.md) | Layering, the C++/ObjC boundary, invariants |
| [C++ API](docs/api.md) | Using `ShirayukiMemory/` directly |
| [Development](docs/development.md) | Test suite, verification without a device |
| [Release](docs/release.md) | Versioning and how a release is cut |

Contributor conventions live in [CLAUDE.md](CLAUDE.md).

---

> For security research, CTF challenges, and educational use on devices you own.
