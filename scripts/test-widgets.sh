#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/widgets-tests"
mkdir -p "$build" "$root/.build/audit-config/gtk-4.0"
printf '[Settings]\ngtk-enable-animations=false\n' > "$root/.build/audit-config/gtk-4.0/settings.ini"
bash "$root/scripts/build-widgets.sh" --dev
source "$root/scripts/backend.sh"
# The C bridge's own lifecycle test; the Tarn backend has tests/native.
if ui_c_backend; then
    cc -std=c11 -Wall -Wextra -Werror -g -DTARN_UI_TESTING \
        "$root/backends/linux/gtk/bridge.c" "$root/tests/widgets/lifecycle.c" \
        -o "$build/lifecycle" $(pkg-config --cflags --libs gtk4)
    timeout 30s "$build/lifecycle"
fi
python3 "$root/tests/widgets/test_widgets.py"
