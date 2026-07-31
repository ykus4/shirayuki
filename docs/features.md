# Features

Eight tabs, plus a few capabilities that exist as a C++ API but are not wired to
the UI yet. Those are called out explicitly rather than implied.

## 🔍 Search

```
First scan         →  Narrow: Changed     →  Narrow: Exact 42
──────────────        ──────────────────     ────────────────
2000 results          87 results             3 results
0x1A001234            0x1A001234             0x1A001234  ✓
0x1A001238            0x1A001238             0x1A005580  ✓
0x1A001240            ...                    0x1B000020  ✓
```

Enter a value and tap scan. Once there are results, the narrow bar appears and the
action button switches to filtering.

### Value types

| Type | Input example |
|---|---|
| `int8` `int16` `int32` `int64` | `100`, `-5`, `0xFF` |
| `uint8` `uint16` `uint32` `uint64` | `255` |
| `float` `double` | `3.14` |
| `hex` — IDA-style pattern | `FF 43 01 D1 ?? ?? ??` |
| `string` | `PlayerName` |
| `regex` | `HP:[0-9]+` |

Tap the type button to cycle. Input accepts an optional sign and a `0x` prefix.

!!! note "Malformed input is rejected, not truncated"
    A value that is empty, unparseable, out of range for its type, or has trailing
    characters is refused with an error. It used to be silently coerced — `"12abc"`
    became `12`, and `300` as an `int8` became `44`.

### Narrowing filters

Available in the narrow bar: **Changed · Unchanged · Inc · Dec**, plus **Reset**.
Entering a value instead of tapping a filter narrows to an exact match.

Comparisons are **numeric**, honouring sign and floating point. A bytewise compare
gets this wrong on little-endian ARM64 — `1 → 256` is an increase, but its low byte
decreases.

!!! info "Core API only"
    `CompareMode::GreaterThan` and `LessThan` exist in `Scanner::narrowResults` but
    have no narrow-bar buttons yet.

### Batch modify and export

Long-press the action button for **Export to JSON** and **Batch Modify**. Batch
modify reports how many addresses were actually written, not just how many were
attempted.

## 🔧 Patch

```
Address      Original       Patched        State
──────────── ────────────── ────────────── ──────
0x1A001234   FF 43 01 D1    1F 20 03 D5    ✅ ON
0x1A005580   E0 03 00 AA    1F 20 03 D5    ⏸ OFF
             ↑ auto backup                 ↑ toggle
```

Input is `0xADDR HEXBYTES [label]`. Original bytes are captured before the first
write, so a patch can be toggled off. Tap a row to toggle, swipe to delete
(restoring the original first), long-press for undo/redo.

## 🔒 Freeze

```
0x1A001234  =  99999  (float)  [FROZEN]   ← tap to pause
0x1A001238  =  1      (int32)  [INC +1]   ← auto-increment each tick
0x1A00123C  =  100    (int32)  [PAUSED]   ← tracked but not writing
```

Input is `0xADDR VALUE [type]`. A background worker rewrites the value every
16 ms. Long-press a row to toggle auto-increment.

!!! info "Core API only"
    `FreezeManager::addConditional` supports writing only while a threshold
    condition holds. No UI for it yet.

## 👁 Watch

```
0x1A001234   float    42.0 → 43.0   ▲  (changed 7x)
0x1A001238   int32    99  → 99      ·  (unchanged)
0x1A00123C   int32    0   → 255     ▲  (changed 1x)
```

Input is `0xADDR [type]`. A 100 ms poll records previous → current and a change
count; the table refreshes twice a second while there are entries.

!!! info "Core API only"
    `WatchManager::setTrigger` attaches a compare mode, threshold and one-shot
    flag, firing a callback outside the manager lock. No UI for it yet.

## 🌿 Pointer scan

```
Target: 0x1A001234

Chain 1: [UnityFramework + 0x1234AB] → +0x10 → +0x28 → +0x00  ✓
Chain 2: [GameLib + 0xABCD00] → +0x08 → +0x00                  ✓
Chain 3: [GameLib + 0xABCD10] → +0x08 → +0x00                  ??
```

Input is `0xADDR [depth] [maxOffset]`. Finds module-relative chains that resolve
to the target, so an address survives relaunches and ASLR. Chains are stored as
`module + offset` plus a list of dereference offsets.

## 💾 Dump

```
0xADDR len      →  hex dump
0xADDR asm      →  ARM64 disassembly

0x1A001234  FF 43 01 D1   STP  x29, x30, [sp, #-0x10]!
0x1A001238  FD 03 00 91   MOV  x29, sp
0x1A00123C  1F 20 03 D5   NOP                           ← long-press to NOP
```

The disassembler covers common ARM64 encodings — branches, loads/stores, moves,
arithmetic — and falls back to raw opcodes otherwise.

!!! warning "A NOP from this tab is not undoable"
    It is written directly rather than through the patch list, so it does not
    appear on the Patch tab and cannot be toggled back.

## 🧵 Threads · 📦 Modules

```
Threads                              Modules
────────────────────────────────     ─────────────────────────────────
tid 0x103  PC 0x1A2B3C4D  RUNNING    UnityFramework   0x104B00000
tid 0x207  PC 0x1B0000A0  WAITING    GameLib          0x1051C0000
```

Threads enumerates via `task_threads()` and reads ARM64 PC/SP/LR plus run state.
Modules lists loaded images with a substring filter; tap to copy the base address,
long-press to copy the full path — which feeds directly into an image-scoped
pattern scan.

## Sessions

Bookmarks, patches, freezes, pointer chains and search history are saved to
`~/Documents/Shirayuki/<bundleid>_autosave.json` when the app backgrounds.

Addresses persist as hex strings and types as tags, so a value above 2^53 and a
future reordering of the `ValueType` enum both survive a round trip. Saves go
through a temp file and rename, so an interrupted save cannot replace a good
session with a truncated one.

## Not functional yet

Shipped as API surface only, needing on-device work:

- **Speedhack** — `Speedhack::setScale/install` is stable, but the
  `mach_absolute_time` interposition is unimplemented.
- **Hotkeys** — `SYHotkey bind:action:` exists; multi-finger gesture hit-testing
  is unimplemented.
- **Group scan** (`GroupScan`) — multi-field struct matching, e.g. `{HP, MP, state}`.
- **Snapshot diff** (`Snapshot`) — capture a range and diff two captures.
- **Read cache** (`ReadCache`) — per-thread TTL cache for the poll loops.

The last three are complete and usable from C++; they simply have no tab.
