# Development

## Verification without Theos or a device

Theos is only needed to build the tweak. Everything below runs on any machine with
Xcode:

```bash
make test           # build + run the C++ core suite (5 suites, 527 checks)
make syntax-check   # compile AND link every source against the iPhoneOS SDK
make fmt            # clang-format in place
make fmt-check      # clang-format, failing on any diff
make check          # fmt-check + test + syntax-check
```

Run `make check` before committing.

## Why the core is testable

`ShirayukiMemory/` is pure C++ (see [Architecture](architecture.md#the-layering-rule)),
and the Mach VM APIs it uses — `vm_read_overwrite`, `vm_write`,
`vm_region_recurse_64`, `vm_protect`, `_dyld_*`, `getsegmentdata` — all exist on
macOS. The suite therefore performs **real** reads, writes, region walks, patches
and scans against the test process's own memory. It is not a mock.

`tests/CMakeLists.txt` globs `ShirayukiMemory/*.cpp`, so a new core file is picked
up automatically.

| Suite | Covers |
|---|---|
| `test_valuetype` | sizes, tag vocabulary, parse strictness and range limits, format/parse round trips, `compareTypedBytes` ordering |
| `test_memory` | read/write, unmapped-address handling, region enumeration and filters, region labels, patch apply/restore, hex conversion and dump bounds, image and symbol lookup |
| `test_scanner` | value/pattern/string/regex scans, stride and alignment, chunk-boundary matches, scanning an unmapped range, all narrowing modes |
| `test_workers` | freeze holds a value, pause/resume/remove, conditional threshold ordering, watch change detection, prompt `stop()`, repeated start/stop, interval clamping |
| `test_session` | JSON scalars, malformed input rejection, escape round trips, unicode, large addresses, full session save/load, corrupt-file handling |

There is no external test framework — `tests/syharness.hpp` is a few dozen lines of
`SY_CHECK` macros, so `cmake && ctest` works on a bare machine.

### Writing a test

```cpp
#include "ShirayukiMemory.hpp"
#include "syharness.hpp"

using namespace Shirayuki;

static void testSomething() {
    volatile int32_t target = 100;
    int32_t out = 0;
    SY_CHECK(Memory::readValue<int32_t>((uintptr_t)&target, out) == Status::Success);
    SY_CHECK_EQ(out, 100);
}

static void run() {
    testSomething();
}

SY_MAIN("test_something")
```

Drop it in `tests/` as `test_*.cpp`; CMake finds it. Each test has a 60-second
timeout so a runaway loop or an unbounded allocation fails instead of wedging CI —
which has already happened once, when a test caught `Hex::dump` allocating a
caller-supplied length and it grew to 10.9 GB.

!!! warning "Do not edit a test to make it pass"
    Several tests assert that a previous implementation was wrong — for instance
    that a bytewise compare gives the *opposite* answer to `compareTypedBytes`. If a
    test looks wrong, it is more likely documenting a fixed bug.

## The iOS compile and link check

`make syntax-check` runs two phases against the real iPhoneOS SDK:

1. **syntax** — `-fsyntax-only` over every `.mm`, `.m` and `.cpp`.
2. **link** — compiles each to an object and links a dylib.

Phase 2 exists because phase 1 structurally cannot catch cross-translation-unit
symbol mismatches. A C function declared without `extern "C"` and called from a
`.mm` file type-checks fine in both files and fails only at link time. That reached
CI once; the link phase now reproduces it locally.

Only `Tweak/Tweak.xm` is excluded — it needs the Logos preprocessor. It defines the
`%ctor` and hook and nothing references it, so leaving it out creates no undefined
symbols.

!!! note "What this does not cover"
    Logos preprocessing of `Tweak.xm`, `.deb` packaging, and all runtime behaviour
    inside a host app. The `Build` workflow covers the first two. The third needs a
    device.

## Building the tweak

```bash
export THEOS=~/theos
export THEOS_DEVICE_IP=192.168.1.42

make                    # debug build
make package            # .deb into packages/
make package install    # build, install over SSH, respring
```

Without `$THEOS`, `make`/`package`/`install` print a clear error and the
verification targets still work.

## CI

| Workflow | Trigger | What it proves |
|---|---|---|
| `build.yml` | push / PR to `main`, `dev` | full Theos build, uploads a `.deb`, comments on the PR |
| `test.yml` | push / PR | core suite runs; every ObjC/ObjC++ source compiles and links |
| `format.yml` | push / PR | `clang-format` is clean |
| `docs.yml` | push to `main` | this site builds and deploys |
| `release.yml` | `v*` tag | builds and publishes a release |

`test.yml` needs neither Theos nor an SDK tarball, so it finishes in under a minute.

## Code style

- `clang-format` (`.clang-format`, LLVM-based, 4-space indent, 100 columns)
- C++17 via `-std=gnu++17`; `-fobjc-arc`
- Use `__weak`/`__strong` self in escaping blocks

Contributor conventions and the invariants that matter when changing the core are
in [CLAUDE.md](https://github.com/ykus4/shirayuki/blob/main/CLAUDE.md) and
[Architecture](architecture.md#invariants-worth-knowing).

## Previewing these docs

```bash
pip install mkdocs-material
mkdocs serve       # http://127.0.0.1:8000
```
