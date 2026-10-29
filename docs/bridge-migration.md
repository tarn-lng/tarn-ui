# Migrar el bridge GTK a Tarn

Objetivo: que el backend Linux de Tarn UI sea código Tarn que llama a GTK 4
directamente por FFI, sin `bridge.c` ni `devtools.inc`. La API pública
(`core/session.tarn` y lo que hay encima) no cambia para las apps.

## Lo que ya está probado (G0)

Un programa en Tarn puro (`tarn 0.1.1`, `--link gtk-4 --link glib-2.0
--link gobject-2.0`) hizo todo lo que el bridge necesita y que podía chocar
con los límites de la FFI:

| Necesidad | Cómo, sin C propio |
|---|---|
| Señales con contexto | `g_signal_connect_data(obj, señal, ffi.callback(f), datos, null, 0)` |
| Acción por widget | `g_object_set_data` / `g_object_get_data` con el id como dirección |
| Eventos desde callbacks | los callbacks no pueden tocar structs Tarn: escriben en memoria GLib (`GQueue`, bloque `g_malloc0` de escalares con `ffi.write`) |
| Snapshot de texto | `g_strdup(gtk_editable_get_text(...))` en el callback; Tarn lo copia y lo libera |
| `GtkTextIter` | 128 bytes propiedad de un `Vec<u8>`, pasados con `ffi.slice_mut` |
| Accesibilidad (variádica en C) | `gtk_accessible_update_property_value` con `GValue` de 24 bytes en un `Vec<u8>` |
| Propiedades de construcción (`accessible-role`) | `g_object_new_with_properties` (no variádica) |
| Arreglos `char **` | `Vec<usize>` de direcciones `CString` terminado en 0 |

Notas: GTK emite `clicked` de `gtk_widget_activate` tras un timeout, igual que
desde C. Los punteros no retienen préstamos: cada `CString`, `Vec` o bloque
pasado a GTK debe vivir mientras GTK lo use.

## Diseño

- `backends/linux/gtk/gtk.tarn`: declaraciones `extern "C"` de GTK, GLib y
  GObject, sin lógica.
- `backends/linux/gtk/native.tarn`: el backend. Expone las mismas operaciones
  que `bridge.h` (mismos códigos de estado y de evento), así `session.tarn`
  solo cambia de llamadas `extern` a llamadas Tarn.
- Estado en dos partes:
  - Tarn (`Native`): ventana, registro clave→widget (`HashMap<string, *mut Widget>`),
    páginas, regiones, todo lo que solo toca el hilo que llama a la API.
  - Memoria GLib para lo que escriben los callbacks: banderas (`opened`,
    `closed`, `hidden`, `woken`, `queue_full`, `background`) y la cola de
    eventos (registros `code`, `token`, `text*`, máximo 64).
- Hilo: igual que hoy, solo el hilo principal (`gettid == getpid`).

## Etapas

| | Alcance | Prueba de cierre |
|---|---|---|
| G1 ✓ | `gtk.tarn`, bloque de banderas, cola de eventos; ciclo de vida: create, next, request_close, release, finish, wake, background, present/hide, is_visible; contador | smoke Tarn del ciclo de vida y contador, sin fugas de sesión |
| G2 ✓ | páginas, etiquetas, botones, cajas, texto, entradas, `set_*`, foco, accesibilidad | `tests/native/components.tarn`: los mismos casos que `session-smoke`, más finalización de widgets |
| G3 | listas y regiones (`list_*`, `tree_*`) | ejemplos de navegación y regiones |
| G4 | controles, indicadores, contenedores, texto multilínea, barra de título y menú | ejemplo `widgets` |
| G5 | devtools (`ui.inspect`, `ui.fill`, `ui.click`, `ui.close`) en Tarn; json-glib o JSON propio | suites Python (widgets, navigation, reactive, devtools, tray) sin cambios |
| G6 | `session.tarn` usa el backend Tarn por defecto; se borran `bridge.c`, `bridge.h`, `devtools.inc`, scripts de compilación de C | todas las suites + ASan sobre el binario Tarn |

Hasta G6 el bridge C sigue siendo el predeterminado y `session.tarn` no
cambia. El backend Tarn se prueba por su cuenta con `scripts/test-native.sh`,
que compila cada `tests/native/*.tarn` y lo ejecuta con
`G_DEBUG=fatal-criticals`. Las pruebas C del bridge
(`tests/gtk/bridge*`, `test_hooks.h`) se sustituyen por smokes Tarn que
comprueban lo mismo (handlers desconectados, ventana finalizada).
