# Usage

A walkthrough of the workflow that actually gets used: find a value, confirm it,
change it, keep it changed, and make the address survive a relaunch.

## The panel

Tap the snowflake to open it. Drag the title bar to move it, tap ✕ to close.

```
┌──────────────────────────────┐
│ ❄️ Shirayuki               ✕ │  ← drag here
├──────────────────────────────┤
│ 🔍 🔧 🔒 👁 🌿 💾 🧵 📦      │  ← tabs, scrollable
├──────────────────────────────┤
│ [i32] [ value or pattern ] ▶ │  ← type · input · action
├──────────────────────────────┤
│ Changed Unchanged Inc Dec ⟲  │  ← narrow bar (Search, after a scan)
├──────────────────────────────┤
│ 0x104C2A100   = 100 (0x64)   │
│ 0x104C2A180   = 100 (0x64)   │  ← tap a row to act on it
│ …                            │
└──────────────────────────────┘
```

Three controls drive everything:

- **Type button** (`i32`) — tap to cycle the value type.
- **Input field** — the value, pattern or address. Tap it on the Search tab to see
  recent entries.
- **Action button** (▶) — runs the tab's action. **Long-press it** for extra
  actions: export, batch modify, undo/redo, remove all.

Long-pressing a *row* usually copies its address.

## Finding a value you can see

Say the game shows **100 HP**.

**1. Scan for it.** Search tab, type `i32`, enter `100`, tap ▶.

```
2000 results (limit reached — narrow further)
```

Thousands of matches is normal — `100` is a common number. The message tells you
when the result cap was hit, so you know the set is truncated.

**2. Change the value in-game.** Take damage. Now HP is 87.

**3. Narrow.** Enter `87` and tap ▶ — or tap **Changed** if you do not know the new
value.

```
3 results
0x104C2A100   = 87 (0x57)
0x1051C0240   = 87 (0x57)
0x1063B8000   = 87 (0x57)
```

**4. Repeat until one or two remain.** Take damage again, narrow again. Two or
three rounds is usually enough.

!!! tip "Wrong type is the most common reason a scan finds nothing"
    A displayed `100` is very often a `float`. If `i32` finds nothing, cycle to
    `f32` and try again. Health and currency are frequently floats; counters and
    levels are usually integers.

**5. Write to it.** Tap the row. The dialog shows the current value and lets you
write a new one.

```
┌────────────────────────────┐
│ 0x104C2A100                │
│ Current: 87 (0x57) (int32) │
│ ┌────────────────────────┐ │
│ │ 87                     │ │
│ └────────────────────────┘ │
│ [Write] [Freeze] [Watch]   │
│ [Cancel]                   │
└────────────────────────────┘
```

- **Write** — set it once.
- **Freeze** — hand it to the Freeze tab so it is held there.
- **Watch** — hand it to the Watch tab to observe changes.

The field is prefilled with a re-parseable form of the current value, and accepts
negative numbers, decimals and `0x` hex.

!!! note "An unreadable address is reported, not shown as 0"
    If the address has been freed since the scan, the dialog refuses to open rather
    than showing `0` — accepting that prefill would have written a real zero.

## When you cannot see the value

For a value with no on-screen number — a hidden cooldown, an invisible flag —
scan for `?` on a numeric type. That seeds every aligned slot in writable memory
as a candidate, and you then narrow purely by behaviour:

1. Enter `?`, tap ▶.
2. Do the thing that changes the value.
3. Tap **Changed**.
4. Do something that does *not* change it, tap **Unchanged**.
5. Alternate until the set is small.

**Inc** and **Dec** are the useful ones for values that drift in one direction —
a timer counting down, score going up. Comparisons are numeric, so this works
correctly for negative and floating point values.

## Keeping a value fixed

Freeze tab. Input is `0xADDR VALUE [type]`:

```
0x104C2A100 99999 i32
```

A background worker rewrites it every 16 ms.

```
0x104C2A100  = 99999 (int32)  [FROZEN]
```

- **Tap** a row to pause/resume it (`PAUSED` means still tracked, not writing).
- **Long-press** to toggle auto-increment, which adds 1 each tick instead of
  holding a constant.
- **Swipe** to delete.
- **Long-press the action button** → Remove All.

Type tags accept both short and long forms — `i32` and `int32` are the same, as are
`f32`/`float`.

## Watching a value change

Watch tab, input `0xADDR [type]`:

```
0x104C2A100   float    42.0 → 43.0   ▲  (changed 7x)
```

Polls every 100 ms and shows previous → current with a change count. Useful for
working out *when* something is written, and for telling a real match from a
coincidence.

## Patching code

Dump tab first, to see what you are changing:

```
0x104C31200 asm
```

```
0x104C31200  FF 43 01 D1   STP  x29, x30, [sp, #-0x10]!
0x104C31204  FD 03 00 91   MOV  x29, sp
0x104C31208  E0 03 00 AA   MOV  x0, x1
```

Then Patch tab, `0xADDR HEXBYTES [label]`:

```
0x104C31208 1F 20 03 D5 skip damage
```

```
0x104C31208   E0 03 00 AA → 1F 20 03 D5   [ON]  skip damage
```

Original bytes are captured before the write, so:

- **Tap** to toggle the patch off and back on.
- **Swipe** to delete — the original bytes are restored first.
- **Long-press the action button** for Undo / Redo / Restore All.

`1F 20 03 D5` is ARM64 `NOP`. The Dump tab also has a long-press → NOP shortcut,
but that one is **not** tracked and cannot be undone — prefer the Patch tab if you
might want it back.

## Making an address survive a relaunch

Heap addresses change every launch. A pointer chain does not.

Pointer tab, input the address you found, optionally a depth:

```
0x104C2A100 3
```

```
[UnityFramework + 0x1234AB] → +0x10 → +0x28 → +0x00   ✓
[GameLib + 0xABCD00] → +0x08 → +0x00                  ✓
```

Each chain starts at a module-relative offset, so it is stable across launches and
ASLR. Shorter chains are usually more reliable. Tap a row to copy the notation.

Chains are saved in the session file, so they come back next launch.

## Finding code by pattern

When you want the same routine in a future build, search for its bytes rather than
its address.

1. **Modules tab** — long-press the module to copy its full path.
2. **Dump tab** — disassemble around the interesting code.
3. **Search tab**, type `hex`, enter an IDA-style pattern with `??` for bytes that
   will move:

```
FF 43 01 D1 ?? ?? ?? ?? E0 03 00 AA
```

Wildcards let the pattern survive recompilation.

## Sessions

Bookmarks, patches, freezes, pointer chains and search history are written to
`~/Documents/Shirayuki/<bundleid>_autosave.json` when the app goes to the
background, and reloaded on the next launch.

## Practical notes

- **Scans are capped** at 2000 results and skip regions over 100 MB. If you see
  `limit reached`, narrow rather than trusting the list to be complete.
- **A scan runs off the main thread**, so the app keeps running. Tapping the action
  button again while one is in flight is refused rather than starting a second.
- **Freezing a stale address does nothing visible.** If a freeze stops working
  after a relaunch, the address moved — find it again, or use a pointer chain.
- **Injecting into every app includes system apps.** If SpringBoard misbehaves,
  restrict the [injection filter](install.md#restricting-which-apps-it-injects-into).
- **Nothing here is verified on device by CI.** The build and the C++ core are
  tested automatically; panel behaviour is not.
