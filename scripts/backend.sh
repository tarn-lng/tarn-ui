# Sourced by the build scripts: places the Tarn GTK backend beside a copied
# core/session.tarn. Usage: ui_backend BUILD_DIR [-DTARN_UI_DEVTOOLS]
# [-DTARN_UI_TESTING]; then link with "${UI_LINKS[@]}". DEVTOOLS selects
# gtk/devtools.tarn (else the empty devtools_off.tarn); TESTING builds the
# fixtures' test hooks (tests/native/hooks.c) as libtarn_ui_gtk.so.
ui_backend() {
    local build=$1
    shift
    local gtk="$root/backends/linux/gtk"
    local dev=0 testing=0 flag
    for flag in "$@"; do
        case "$flag" in
        -DTARN_UI_DEVTOOLS) dev=1 ;;
        -DTARN_UI_TESTING) testing=1 ;;
        *) echo "ui_backend: unknown flag $flag" >&2; return 2 ;;
        esac
    done
    mkdir -p "$build/source/gtk" "$build/source/core"
    cp "$gtk/gtk.tarn" "$gtk/native.tarn" "$gtk/protocol.tarn" "$build/source/gtk/"
    if (( dev )); then cp "$gtk/devtools.tarn" "$build/source/gtk/devtools.tarn"
    else cp "$gtk/devtools_off.tarn" "$build/source/gtk/devtools.tarn"; fi
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
