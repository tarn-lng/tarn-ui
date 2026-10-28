#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
evidence="$root/.build/evidence/phase5-audit"
mkdir -p "$evidence"
bash "$root/scripts/test-navigation.sh" > "$evidence/navigation.log" 2>&1
bash "$root/scripts/test-reactive.sh" > "$evidence/reactive.log" 2>&1
cc -std=c11 -Wall -Wextra -Werror "$root/tests/audit/monitors.c" \
    -o "$evidence/monitors" $(pkg-config --cflags --libs gtk4)
"$evidence/monitors" > "$evidence/monitors.json" 2> "$evidence/monitors.stderr"
gate=0
python3 "$root/tests/audit/phase5.py" > "$evidence/layout.stdout.json" 2> "$evidence/layout.stderr" || gate=$?
{
    date -u +%Y-%m-%dT%H:%M:%SZ
    pkg-config --modversion gtk4 json-glib-1.0
    sha256sum "$root/core/session.tarn" "$root/core/components.tarn" "$root/core/reactive.tarn" \
        "$root/core/navigation.tarn" "$root/core/forms.tarn" "$root/backends/linux/gtk/bridge.c" \
        "$root/backends/linux/gtk/bridge.h" "$root/backends/linux/gtk/devtools.inc" \
        "$root/examples/navigation/main.tarn" "$root/examples/navigation/tasks.tarn" \
        "$root/tests/navigation/lifecycle.c" "$root/tests/navigation/test_navigation.py" \
        "$root/tests/audit/phase5.py" "$root/tests/audit/monitors.c" "$root/scripts/audit-phase5.sh"
} > "$evidence/provenance.txt"
cp "$root/.build/navigation-dev/provenance.txt" "$evidence/build-provenance.txt"
cp "$root/.build/navigation-tests/lifecycle.log" "$evidence/lifecycle.log"
cp "$root/.build/navigation-tests/tasks.log" "$evidence/tasks.log"
cp "$root/.build/navigation-tests/forms.log" "$evidence/forms.log"
printf 'Audit evidence: %s\n' "$evidence"
cat "$evidence/navigation.log" "$evidence/reactive.log" "$evidence/layout.stdout.json"
exit "$gate"
