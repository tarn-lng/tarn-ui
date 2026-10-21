#!/usr/bin/env bash
set -euo pipefail
[[ $(uname -sm) == 'Linux x86_64' ]]
for tool in git rustc cargo cc pkg-config; do command -v "$tool"; done
rustc --version
cargo --version
cc --version | head -n 1
pkg-config --modversion gtk4
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cat > "$work/gtk.c" <<'C'
#include <gtk/gtk.h>
int main(void) { return gtk_get_major_version() == 4 ? 0 : 1; }
C
# pkg-config output is intentionally split into compiler arguments, never eval'd.
cc -Wall -Wextra -Werror "$work/gtk.c" -o "$work/gtk" $(pkg-config --cflags --libs gtk4)
"$work/gtk"
printf 'GTK4 headers, linker and runtime verified (no display opened).\n'
