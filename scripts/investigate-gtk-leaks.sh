#!/usr/bin/env bash
# Separate diagnosis: all LSan failures are retained, including GTK-only runs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_binary="$root/.build/gtk-tests/lifecycle"
evidence="$root/.build/evidence/leak-investigation"
[[ -x "$test_binary" ]] || { echo 'Run: bash scripts/test-gtk.sh --build-only' >&2; exit 1; }
mkdir -p "$evidence"
pkg-config --modversion gtk4 pango fontconfig > "$evidence/versions.txt"
result=0
for renderer in default cairo; do
    for cycles in 1 16; do
        name="$renderer-$cycles"
        status=0
        args=(env -u GSK_RENDERER)
        if [[ $renderer != default ]]; then args=(env "GSK_RENDERER=$renderer"); fi
        "${args[@]}" ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
            UBSAN_OPTIONS=halt_on_error=1 timeout 30s "$test_binary" \
            --gtk-control "$evidence/$name.png" "$cycles" \
            > "$evidence/$name.log" 2>&1 || status=$?
        printf '%s\n' "$status" > "$evidence/$name.exit"
        printf '%s: exit %s\n' "$name" "$status"
        (( status == 0 )) || result=1
    done
done
exit "$result"
