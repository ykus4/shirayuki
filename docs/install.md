# Installation

## Requirements

| | |
|---|---|
| Device | iPhone or iPad, **arm64** |
| iOS | **15.0 or newer** |
| Jailbreak | Any, but see the rootless note below |
| Hooking runtime | **ElleKit** (rootless) or **Substrate / Substitute** (rooted) |

## Rooted vs rootless — pick the right package

iOS 15 and later are jailbroken with **rootless** tools: palera1n, Dopamine,
XinaA15. Rootless jailbreaks cannot write to `/Library`, so tweaks live under
`/var/jb` instead.

Shirayuki's releases are built **rootless**, because that covers every current
iOS 15+ jailbreak. If you are on an older rooted jailbreak (unc0ver, checkra1n,
Taurine), the release `.deb` will install but the tweak will never load — you need
a rooted build:

```bash
make THEOS_PACKAGE_SCHEME= package
```

Not sure which you have? If your package manager is **Sileo on Dopamine/palera1n**,
or paths appear under `/var/jb`, you are rootless.

## Installing the release

1. Download the `.deb` from
   [Releases](https://github.com/ykus4/shirayuki/releases).
2. Install it either way:

=== "Package manager (recommended)"

    Open the `.deb` with Sileo, Zebra or Installer. They resolve the ElleKit or
    Substrate dependency for you.

=== "Command line"

    ```bash
    # rootless
    sudo dpkg -i com.yotti.shirayuki_0.2.0_iphoneos-arm64.deb
    sudo uicache --all

    # rooted
    dpkg -i com.yotti.shirayuki_0.2.0_iphoneos-arm64.deb
    killall -9 SpringBoard
    ```

3. Respring, or force-quit and reopen the app you want to inspect.

## Building from source

```bash
# Install Theos first: https://theos.dev/docs/installation
export THEOS=~/theos
export THEOS_DEVICE_IP=192.168.1.42     # your device

git clone https://github.com/ykus4/shirayuki.git
cd shirayuki
make package install
```

`make package install` builds, copies over SSH, installs and resprings.

To build without a device attached:

```bash
make package            # .deb lands in packages/
```

See [Development](development.md) for the checks that run without Theos at all.

## Verifying it loaded

After respringing, open any app. A **draggable snowflake button** appears near the
top-left after about 1.5 seconds. Tap it to open the panel.

If nothing appears:

??? question "No snowflake button at all"
    - Confirm the tweak is installed: `ls /var/jb/Library/MobileSubstrate/DynamicLibraries/Shirayuki.*`
      (drop `/var/jb` if rooted). You should see both `.dylib` and `.plist`.
    - Check the injection filter. `Shirayuki.plist` must contain a bundle the app
      matches — by default `com.apple.UIKit`, which covers all UIKit apps.
    - Look for load messages: `Shirayuki` logs `[Shirayuki] Loaded` then
      `[Shirayuki] Ready` to the system log. Watch it with Console.app over USB,
      or `oslog`/`idevicesyslog`.

??? question "Installed but nothing injects (rootless)"
    A rooted `.deb` on a rootless jailbreak installs into `/Library/...`, which
    nothing reads. Reinstall the rootless package, or rebuild with the default
    `THEOS_PACKAGE_SCHEME = rootless`.

??? question "Dependency error on install"
    `Depends: ellekit | mobilesubstrate` — install ElleKit (rootless) or
    Substrate/Substitute (rooted) from your package manager first.

??? question "The panel opens but scanning finds nothing"
    Not necessarily a bug. Check the value type matches what the app stores — a
    displayed `100` is often a `float`, not an `int32`. Try the
    [unknown-value workflow](usage.md#when-you-cannot-see-the-value).

## Restricting which apps it injects into

Loading into every UIKit app is convenient but also means it loads into
SpringBoard, Settings and every system app. To limit it to one target, edit
`Shirayuki.plist` before building:

```
{ Filter = { Bundles = ( "com.your.game" ); }; }
```

!!! warning "Keep the plist comment-free"
    It is parsed as an *old-style* plist (not XML) by the injection runtime. A
    parse failure is silent — the tweak simply never loads.

## Uninstalling

Remove it from your package manager, or:

```bash
sudo dpkg -r com.yotti.shirayuki
```

Session files are left behind at
`~/Documents/Shirayuki/` inside each app's container.
