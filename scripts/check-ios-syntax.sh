#!/bin/bash
#
# Compile and link the tweak's sources against the real iPhoneOS SDK.
#
# Theos is not required: this drives clang directly with the same flags the tweak
# is built with, so the whole codebase gets compiler coverage on a machine with
# only Xcode installed.
#
# Two phases:
#   1. syntax — -fsyntax-only over every ObjC/ObjC++/C++ source.
#   2. link   — compile to objects and link a dylib, so cross-translation-unit
#               symbol mismatches are caught. Phase 1 alone cannot see them: a C
#               function declared without `extern "C"` and called from a .mm file
#               type-checks fine and fails only at link time, which is how a
#               broken build reached CI once.
#
# Tweak/Tweak.xm is excluded from both phases because it needs the Logos
# preprocessor. It defines the %ctor and the hook and nothing else references it,
# so leaving it out does not create undefined symbols.
#
# Usage: scripts/check-ios-syntax.sh [--syntax-only]

set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

SDK=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
if [[ -z "$SDK" ]]; then
    echo "iPhoneOS SDK not found — install Xcode and run: sudo xcode-select -s /Applications/Xcode.app"
    exit 2
fi

DEPLOY_TARGET=15.0
OBJ_DIR=$(mktemp -d)
trap 'rm -rf "$OBJ_DIR"' EXIT

# Mirrors Shirayuki_CFLAGS / Shirayuki_CCFLAGS in the Makefile. Keep in sync.
COMMON_FLAGS=(
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

# Language flags per file kind. ObjC++ must use gnu++17, not c++17: plain c++17
# removes `typeof`, which the ARC weak-self idiom depends on.
lang_flags() {
    case "$1" in
    *.mm) echo "-x objective-c++ -std=gnu++17" ;;
    *.m) echo "-x objective-c" ;;
    *.cpp) echo "-x c++ -std=gnu++17" ;;
    esac
}

sources() {
    find ShirayukiMemory GUI \( -name '*.mm' -o -name '*.m' -o -name '*.cpp' \) | sort
}

failures=0
checked=0

# --- Phase 1: syntax -------------------------------------------------------

while IFS= read -r f; do
    if ! out=$(clang $(lang_flags "$f") "${COMMON_FLAGS[@]}" -fsyntax-only "$f" 2>&1); then
        echo "FAIL $f"
        echo "$out"
        failures=$((failures + 1))
    elif [[ -n "$out" ]]; then
        echo "WARN $f"
        echo "$out"
    fi
    checked=$((checked + 1))
done < <(sources)

echo "syntax: checked $checked file(s), $failures failure(s)"

if [[ $failures -ne 0 ]]; then
    exit 1
fi

if [[ "${1:-}" == "--syntax-only" ]]; then
    exit 0
fi

# --- Phase 2: link ---------------------------------------------------------

objects=()
while IFS= read -r f; do
    obj="$OBJ_DIR/$(echo "$f" | tr '/' '_').o"
    if ! out=$(clang $(lang_flags "$f") "${COMMON_FLAGS[@]}" -c "$f" -o "$obj" 2>&1); then
        echo "FAIL (compile) $f"
        echo "$out"
        exit 1
    fi
    objects+=("$obj")
done < <(sources)

if ! link_out=$(clang++ \
    -isysroot "$SDK" \
    -target "arm64-apple-ios${DEPLOY_TARGET}" \
    -dynamiclib \
    -framework Foundation \
    -framework UIKit \
    -framework CoreGraphics \
    -framework QuartzCore \
    -o "$OBJ_DIR/Shirayuki.dylib" \
    "${objects[@]}" 2>&1); then
    echo "FAIL (link)"
    echo "$link_out"
    exit 1
fi

echo "link: ${#objects[@]} object(s) linked cleanly"
