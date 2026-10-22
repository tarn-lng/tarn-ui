#!/usr/bin/env bash
# Separate diagnosis: all LSan failures are retained, including GTK-only runs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/leak-investigation"
test_binary="$build/lifecycle"
evidence="${EVIDENCE_DIR:-$root/.build/evidence/leak-investigation}"
mkdir -p "$build" "$evidence"
# Always exercise current sources rather than a possibly stale test executable.
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTARN_UI_TESTING "$root/backends/linux/gtk/bridge.c" \
    "$root/tests/gtk/lifecycle.c" -o "$test_binary" \
    $(pkg-config --cflags --libs gtk4)
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "$root/tests/gtk/dependency-lifecycle.c" -o "$build/dependency-lifecycle" \
    $(pkg-config --cflags --libs gtk4)
pkg-config --modversion gtk4 pango fontconfig > "$evidence/versions.txt"
ldd "$test_binary" > "$evidence/shared-libraries.txt"
fontconfig_library=$(awk '/libfontconfig\.so/ {print $3; exit}' "$evidence/shared-libraries.txt")
sha256sum "$test_binary" "$build/dependency-lifecycle" \
    "$root/backends/linux/gtk/bridge.c" "$root/tests/gtk/lifecycle.c" \
    "$root/tests/gtk/dependency-lifecycle.c" > "$evidence/provenance.txt"
if [[ -f $fontconfig_library ]]; then sha256sum "$fontconfig_library" >> "$evidence/provenance.txt"; fi
printf 'case\texit\tleaked_bytes\tallocations\n' > "$evidence/summary.tsv"
result=0
cases=(control-1 control-16 bridge dependency dependency-reset)
if [[ ${RUN_DISPLAY_CLOSE_PROBES:-0} == 1 ]]; then cases+=(control-close bridge-close); fi
for renderer in default cairo; do
    for case in "${cases[@]}"; do
        name="$renderer-$case"
        status=0
        args=(env -u GSK_RENDERER -u TARN_UI_TEST_CLOSE_DISPLAY)
        if [[ $renderer != default ]]; then args+=("GSK_RENDERER=$renderer"); fi
        command=("$test_binary" "$evidence/$name.png")
        case "$case" in
            control-1|control-16) command=("$test_binary" --gtk-control "$evidence/$name.png" "${case#control-}");;
            control-close) command=("$test_binary" --gtk-control "$evidence/$name.png" 16);;
            dependency) command=("$build/dependency-lifecycle");;
            dependency-reset) command=("$build/dependency-lifecycle" --release-font-map);;
        esac
        if [[ $case == *-close ]]; then args+=(TARN_UI_TEST_CLOSE_DISPLAY=1); fi
        "${args[@]}" ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
            UBSAN_OPTIONS=halt_on_error=1 timeout 30s "${command[@]}" \
            > "$evidence/$name.log" 2>&1 || status=$?
        printf '%s\n' "$status" > "$evidence/$name.exit"
        # Missing LSan summary is not evidence of zero leaks (e.g. startup failure).
        bytes=unknown allocations=unknown
        summary=$(sed -nE 's/^SUMMARY: AddressSanitizer: ([0-9]+) byte\(s\) leaked in ([0-9]+) allocation\(s\)\./\1 \2/p' "$evidence/$name.log")
        if [[ -n $summary ]]; then read -r bytes allocations <<< "$summary";
        elif (( status == 0 )); then bytes=0 allocations=0; fi
        printf '%s\t%s\t%s\t%s\n' "$name" "$status" "$bytes" "$allocations" >> "$evidence/summary.tsv"
        printf '%s: exit %s\n' "$name" "$status"
        (( status == 0 )) || result=1
    done
done
exit "$result"
