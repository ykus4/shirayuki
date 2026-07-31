#!/bin/bash
#
# Type-check the ObjC / ObjC++ sources against the real iPhoneOS SDK.
#
# Theos is not required: this drives clang directly with the same flags the
# tweak is built with, so the GUI layer gets compiler coverage even on a machine
# with only Xcode installed. Tweak/Tweak.xm is skipped because it needs the
# Logos preprocessor.
#
# Usage: scripts/check-ios-syntax.sh [--verbose]

set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

SDK=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
if [[ -z "$SDK" ]]; then
    echo "iPhoneOS SDK not found — install Xcode and run: sudo xcode-select -s /Applications/Xcode.app"
    exit 2
fi

DEPLOY_TARGET=15.0

# Mirrors Shirayuki_CFLAGS / Shirayuki_CCFLAGS in the Makefile. Keep in sync.
COMMON_FLAGS=(
    -fsyntax-only
    -isysroot "$SDK"
    -target "arm64-apple-ios${DEPLOY_TARGET}"
    -fobjc-arc
    -IShirayukiMemory
    -IGUI
    -IGUI/Handlers
    -Wall
    -Wextra
    -Wno-unused-parameter
)

failures=0
checked=0

check() {
    local file=$1
    shift
    if ! clang "$@" "${COMMON_FLAGS[@]}" "$file" 2>&1 | tee /tmp/sy-syntax.$$ >/dev/null; then
        echo "FAIL $file"
        cat /tmp/sy-syntax.$$
        failures=$((failures + 1))
    elif [[ -s /tmp/sy-syntax.$$ ]]; then
        echo "WARN $file"
        cat /tmp/sy-syntax.$$
    fi
    rm -f /tmp/sy-syntax.$$
    checked=$((checked + 1))
}

# ObjC++ — must match the Makefile's C++ standard, including GNU extensions:
# plain -std=c++17 removes `typeof`, which ARC weak-self idioms rely on.
while IFS= read -r f; do
    check "$f" -x objective-c++ -std=gnu++17
done < <(find GUI ShirayukiMemory -name '*.mm' | sort)

# Plain ObjC
while IFS= read -r f; do
    check "$f" -x objective-c
done < <(find GUI -name '*.m' | sort)

echo "checked $checked file(s), $failures failure(s)"
[[ $failures -eq 0 ]]
