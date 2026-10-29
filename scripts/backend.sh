# Sourced by the build scripts: prepares the GTK backend for a Tarn build.
# TARN_UI_BACKEND=c (default) compiles bridge.c into libtarn_ui_gtk.so;
# TARN_UI_BACKEND=tarn copies the Tarn backend (backends/linux/gtk/*.tarn)
# and its session over core/session.tarn, and builds the test hooks as
# libtarn_ui_gtk.so when TARN_UI_TESTING is among the flags.
# Usage: ui_backend BUILD_DIR [C flags...]; then link with "${UI_LINKS[@]}"
# ("${UI_PLAIN_LINKS[@]}" for programs that never call the bridge).
ui_c_backend() { [[ ${TARN_UI_BACKEND:-c} == c ]]; }
ui_backend() {
    local build=$1
    shift
    local backend="${TARN_UI_BACKEND:-c}"
    local gtk="$root/backends/linux/gtk"
    local dev=0 testing=0 flag
    for flag in "$@"; do
        [[ $flag == -DTARN_UI_DEVTOOLS ]] && dev=1
        [[ $flag == -DTARN_UI_TESTING ]] && testing=1
    done
    case "$backend" in
    c)
        local packages=(gtk4)
        (( dev )) && packages+=(json-glib-1.0)
        cc -std=c11 -Wall -Wextra -Werror -g -fPIC -shared "$@" "$gtk/bridge.c" \
            -o "$build/libtarn_ui_gtk.so" $(pkg-config --cflags --libs "${packages[@]}")
        UI_LINKS=(--link tarn_ui_gtk --link glib-2.0)
        UI_PLAIN_LINKS=()
        ;;
    tarn)
        mkdir -p "$build/source/gtk" "$build/source/core"
        cp "$gtk/gtk.tarn" "$gtk/native.tarn" "$build/source/gtk/"
        if (( dev )); then cp "$gtk/devtools.tarn" "$build/source/gtk/devtools.tarn"
        else cp "$gtk/devtools_off.tarn" "$build/source/gtk/devtools.tarn"; fi
        cp "$gtk/session.tarn" "$build/source/core/session.tarn"
        UI_LINKS=(--link gtk-4 --link gobject-2.0 --link gio-2.0 --link glib-2.0)
        # Programs that import core/session without a bridge still link GTK.
        UI_PLAIN_LINKS=("${UI_LINKS[@]}")
        if (( testing )); then
            cc -std=c11 -Wall -Wextra -Werror -g -fPIC -shared "$root/tests/native/hooks.c" \
                -o "$build/libtarn_ui_gtk.so" $(pkg-config --cflags --libs gtk4)
            UI_LINKS+=(--link tarn_ui_gtk)
        fi
        ;;
    *) echo "TARN_UI_BACKEND must be c or tarn" >&2; return 2 ;;
    esac
}
