#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/.build/audit-config/gtk-4.0"
printf '[Settings]\ngtk-enable-animations=false\n' > "$root/.build/audit-config/gtk-4.0/settings.ini"
bash "$root/scripts/build-tray.sh" --dev
# Isolated session bus: fixtures play the tray host and the notification server.
NO_AT_BRIDGE=1 GTK_A11Y=none dbus-run-session --config-file="$root/tests/tray/session.conf" -- python3 "$root/tests/tray/test_tray.py" -v
