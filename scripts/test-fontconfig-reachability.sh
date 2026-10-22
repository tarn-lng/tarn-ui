#!/usr/bin/env bash
# Diagnose the system library, then verify the experimental local dependency patch.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 1 && $1 == /* ]] || {
    echo 'Usage: test-fontconfig-reachability.sh /absolute/fontconfig-meson-build' >&2
    exit 2
}
library=$1
[[ -f "$library/libfontconfig.so.1" && -x "$library/fc-cache/fc-cache" ]]
build="$root/.build/fontconfig-reachability"
mkdir -p "$build"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "$root/tests/gtk/fontconfig-offset.c" -o "$build/probe" \
    $(pkg-config --cflags --libs fontconfig)
env -u LD_LIBRARY_PATH ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    "$build/probe" > "$build/system.log" 2>&1
env LD_LIBRARY_PATH="$library" ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    "$build/probe" > "$build/patched.log" 2>&1
rg -q '^LSan while pattern is reachable: 0$' "$build/patched.log"
python3 "$root/tests/gtk/fontconfig_cache_probe.py" "$library" > "$build/cache.json"
sha256sum "$library/libfontconfig.so.1" "$build/probe" > "$build/provenance.txt"
echo "Patched reachability and cache roundtrip passed; system findings retained in $build/system.log"
