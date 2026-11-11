#!/usr/bin/env bash
# Assembles the reusable tarn_ui package from core/ and the GTK backend and
# publishes it to a local registry. Usage: package.sh [REGISTRY_DIR]
# (default .build/registry). Published versions are immutable: bump
# package/tarn.toml before republishing.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
registry="${1:-$root/.build/registry}"
[[ $registry == /* ]] || registry="$PWD/$registry"
compiler="${TARN_BIN:-$(command -v tarn)}"
stage="$root/.build/package/tarn_ui"
gtk="$root/backends/linux/backend"
dbus="${TARN_DBUS:-$root/../libs/dbus}"
rm -rf "$stage"
mkdir -p "$stage/core" "$stage/backend" "$stage/src" "$stage/desktop" "$registry"
cp "$root/package/lib.tarn" "$root/package/tarn.toml" "$root/package/README.md" "$stage/"
cp "$root"/core/{session,reactive,components,navigation,forms,desktop,accessibility,window_capture,remote_desktop,activity}.tarn "$stage/core/"
cp "$gtk/gtk.tarn" "$gtk/native.tarn" "$gtk/protocol.tarn" "$stage/backend/"
# tarn-dbus (pure Tarn) at the paths core/desktop imports; the published
# dbus 0.1.0 lacks desktop/, so it travels inside this package for now.
cp "$dbus"/src/*.tarn "$stage/src/"
cp "$dbus"/desktop/*.tarn "$stage/desktop/"
rm -f "$stage"/src/*_test.tarn "$stage"/desktop/*_test.tarn
# Release package: the devtools transport is compiled out.
cp "$gtk/devtools_off.tarn" "$stage/backend/devtools.tarn"
(cd "$stage" && "$compiler" check lib.tarn >/dev/null)
(cd "$stage" && "$compiler" publish --registry "file://$registry/")
printf 'tarn_ui published to file://%s/\n' "$registry"
