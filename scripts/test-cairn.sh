#!/usr/bin/env bash
# Runs the Cairn backend's tests (backends/cairn/backend/*_test.tarn):
# DamageRegion unit tests, headless PNG snapshot determinism, 1080p B1 budget
# benchmarks, and the live macOS host window / ABI 14 contract smoke tests.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $(uname -s) == Darwin ]] || { echo 'test-cairn.sh currently runs on macOS' >&2; exit 2; }
compiler="${TARN_BIN:-$(command -v tarn)}"
libs="$root/.build/cairn-libs"
mkdir -p "$libs"
sdk=$(xcrun --show-sdk-path)
ln -sf "$sdk/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation.tbd" \
    "$libs/libCoreFoundation.tbd"
ln -sf "$sdk/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics.tbd" \
    "$libs/libCoreGraphics.tbd"
cd "$root/backends/cairn"
LIBRARY_PATH="$libs" "$compiler" test .
