# Sourced by the build scripts: places the platform backend (GTK on Linux,
# AppKit on macOS) beside a copied
# core/session.tarn. Usage: ui_backend BUILD_DIR [-DTARN_UI_DEVTOOLS]
# [-DTARN_UI_TESTING]; then link with "${UI_LINKS[@]}". DEVTOOLS selects
# gtk/devtools.tarn (else the empty devtools_off.tarn); TESTING builds the
# fixtures' test hooks (tests/native/hooks.c) as libtarn_ui_gtk.so.
ui_backend() {
    local build=$1
    shift
    if [[ $(uname -s) == Darwin ]]; then
        # AppKit backend: no devtools channel or test hooks yet.
        local appkit="$root/backends/macos/backend"
        mkdir -p "$build/source/backend" "$build/source/core"
        cp "$appkit/objc.tarn" "$appkit/native.tarn" "$build/source/backend/"
        cp "$appkit/devtools_off.tarn" "$build/source/backend/devtools.tarn"
        cp "$root/core/session.tarn" "$build/source/core/session.tarn"
        # Tarn links with `cc -l<name>`: expose CoreFoundation's SDK stub as a
        # library in the build directory (callers set LIBRARY_PATH to it).
        ln -sf "$(xcrun --show-sdk-path)/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation.tbd" \
            "$build/libCoreFoundation.tbd"
        # Same libraries as backends/macos/tarn.toml [native] links.
        UI_LINKS=(--link objc --link CoreFoundation)
        UI_PLAIN_LINKS=("${UI_LINKS[@]}")
        return 0
    fi
    local gtk="$root/backends/linux/backend"
    local dev=0 testing=0 flag
    for flag in "$@"; do
        case "$flag" in
        -DTARN_UI_DEVTOOLS) dev=1 ;;
        -DTARN_UI_TESTING) testing=1 ;;
        *) echo "ui_backend: unknown flag $flag" >&2; return 2 ;;
        esac
    done
    mkdir -p "$build/source/backend" "$build/source/core"
    cp "$gtk/gtk.tarn" "$gtk/native.tarn" "$gtk/protocol.tarn" "$build/source/backend/"
    if (( dev )); then cp "$gtk/devtools.tarn" "$build/source/backend/devtools.tarn"
    else cp "$gtk/devtools_off.tarn" "$build/source/backend/devtools.tarn"; fi
    cp "$root/core/session.tarn" "$build/source/core/session.tarn"
    # Same libraries as backends/linux/tarn.toml [native] links.
    UI_LINKS=(--link gtk-4 --link gobject-2.0 --link gio-2.0 --link glib-2.0)
    UI_PLAIN_LINKS=("${UI_LINKS[@]}")
    if (( testing )); then
        cc -std=c11 -Wall -Wextra -Werror -g -fPIC -shared "$root/tests/native/hooks.c" \
            -o "$build/libtarn_ui_gtk.so" $(pkg-config --cflags --libs gtk4)
        UI_LINKS+=(--link tarn_ui_gtk)
    fi
}
