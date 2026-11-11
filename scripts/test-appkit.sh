#!/usr/bin/env bash
# Runs the AppKit backend's tests (backends/macos/backend/*_test.tarn) on a
# logged-in macOS session. Clicks use performClick:/sendAction:to:, so no
# Accessibility permission is needed.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $(uname -s) == Darwin ]] || { echo 'test-appkit.sh runs on macOS' >&2; exit 2; }
compiler="${TARN_BIN:-$(command -v tarn)}"
libs="$root/.build/appkit-libs"
mkdir -p "$libs"
# Tarn links with `cc -l<name>`: CoreFoundation's SDK stub as a library.
ln -sf "$(xcrun --show-sdk-path)/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation.tbd" \
    "$libs/libCoreFoundation.tbd"
cd "$root/backends/macos"
LIBRARY_PATH="$libs" "$compiler" test .
