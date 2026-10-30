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

Notas: `gtk_accessible_list_new_from_array` rechaza arreglos válidos en GTK
4.24 (aserción invertida); la relación `labelled-by` usa
`gtk_accessible_list_new_from_list`. GTK emite `clicked` de `gtk_widget_activate` tras un timeout, igual que
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
| G2 ✓ | páginas, etiquetas, botones, cajas, texto, entradas, `set_*`, foco, accesibilidad | `gtk/components_test.tarn`: los mismos casos que `session-smoke`, más finalización de widgets |
| G3 ✓ | listas y regiones (`list_*`, `tree_*`) | `gtk/regions_test.tarn`: identidad, orden, reescritura y eliminación |
| G4 ✓ | controles, indicadores, contenedores, texto multilínea, barra de título y menú | `gtk/widgets_test.tarn`: eventos de usuario, escrituras sin evento, rangos, contenedores, cabecera, menú y sus nodos de región |
| G5 ✓ | devtools en Tarn (`gtk/devtools.tarn`, con `json` y `crypto` del stdlib) y `gtk/session.tarn` | suites Python (widgets, navigation, reactive, devtools, tray) sin cambios con `TARN_UI_BACKEND=tarn` |
| G6 ✓ | `core/session.tarn` es la sesión Tarn; se borran `bridge.c`, `bridge.h`, `devtools.inc`, sus tests en C y `investigate-gtk-leaks.sh`; el lanzador compila el backend Tarn | todas las suites + `test-devtools-memory.sh` (ASan y LSan) sobre el binario Tarn |

Desde G3 el backend usa constantes, `match` como expresión, comparación de
punteros con `==`, `ffi.offset`/`ffi.vec_mut`, `unwrap`/`expect` y
`to_string()` (ADR 0088–0090 del compilador). Necesita `tarn` 0.2.0 o
posterior.

G5 añade:

- `gtk/session.tarn`: la API de `core/session.tarn` sobre `native` y
  `devtools`. Con `TARN_UI_BACKEND=tarn`, `scripts/backend.sh` lo copia como
  `core/session.tarn`, copia el backend a `gtk/` y enlaza GTK; sin la variable
  se compila `bridge.c` como antes.
- `gtk/devtools.tarn` en builds `--dev`, `gtk/devtools_off.tarn` (vacío) en
  las demás: el binario de producción no contiene el canal. Sus fuentes GLib
  (socket, temporizador, after-paint) solo marcan un bloque GLib propio; el
  trabajo ocurre en `pump()`, que la sesión llama antes de bloquear.
- Cada evento encolado lleva el token de la petición en curso, así
  `complete_event` responde a la acción correcta, igual que en C.
- `tests/native/hooks.c`: los hooks de prueba de los fixtures (`test_clean`,
  contadores, `test_dev_*`) para el backend Tarn. Registra sus contadores como
  qdata del tipo GObject; `native.tarn` los encuentra ahí y, sin hooks (una
  app real), el puntero es nulo.

Desde G6 no queda código C en el backend: la sesión, el canal de devtools y
el lanzador usan `backends/linux/gtk/*.tarn`, y json-glib ya no es una
dependencia. El único C que queda es de prueba: `tests/native/hooks.c` (hooks
de los fixtures), `tests/gtk/*.c` de fontconfig y `tests/audit/monitors.c`.