#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/widgets-tests"
mkdir -p "$build" "$root/.build/audit-config/gtk-4.0"
printf '[Settings]\ngtk-enable-animations=false\n' > "$root/.build/audit-config/gtk-4.0/settings.ini"
bash "$root/scripts/build-widgets.sh" --dev
python3 "$root/tests/widgets/test_widgets.py"
