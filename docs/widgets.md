# Controles nativos de formulario

ABI **13** añadió cinco controles GTK nativos a las páginas estáticas: interruptor,
casilla, deslizador, campo numérico y desplegable. El modelo vive en Tarn: cada
cambio del usuario llega como acción con el nuevo valor y la vista vuelve a
escribir los valores desde el modelo sin generar eventos.

```bash
bash scripts/build-widgets.sh
bash scripts/run-widgets.sh
bash scripts/test-widgets.sh
```

## Uso

```tarn
children.push(components.switch("notify", "Notificaciones", true, 1))
children.push(components.check("dark", "Usar tema oscuro", false, 2))
children.push(components.slider("volume", "Volumen", 40, 0, 100, 3))
children.push(components.spin("copies", "Copias", 1, 1, 99, 4))
children.push(components.dropdown("quality", "Calidad", qualities.as_slice(), 1, 5))
```

Con `components.drive_input`, el callback recibe `(acción, texto)` y el texto es
el valor decimal: `0`/`1` para interruptor y casilla, el índice para el
desplegable y el número para deslizador y campo numérico. Para escribir desde el
modelo se usa `View.inputs` con la misma clave y el valor en decimal; por ejemplo,
el botón Restablecer del ejemplo devuelve todos los controles a sus valores iniciales.

| Control | Widget GTK | Rol accesible | Valores |
| --- | --- | --- | --- |
| `switch` | `GtkSwitch` | switch | 0..1 |
| `check` | `GtkCheckButton` | checkbox | 0..1 |
| `slider` | `GtkScale` | slider | min..max, paso 1 |
| `spin` | `GtkSpinButton` | spin-button | min..max, paso 1 |
| `dropdown` | `GtkDropDown` | combo-box | índice de 1..64 opciones |

Interruptor, deslizador, campo numérico y desplegable llevan una etiqueta visible
enlazada con la relación accesible `labelled-by`; la casilla usa su propia
etiqueta. El deslizador coloca la etiqueta encima para usar el ancho completo.

## Límites

- Rangos dentro de ±1 000 000 000, `min <= valor <= max`. Valores fuera de rango,
  texto no numérico o vacío devuelven error sin cambiar el control.
- El texto del título es una sola línea; el desplegable usa la primera línea como
  título y las siguientes como opciones no vacías.
- Por ahora los controles se montan solo en páginas estáticas. Usarlos dentro de
  `View.regions` devuelve error.
- `ui.fill` acepta el valor decimal de un control y rechaza valores no válidos
  con `INVALID_REQUEST`; `ui.inspect` muestra el valor como texto con `allow_text`.

## Pruebas

`tests/widgets/lifecycle.c` comprueba tipos, roles, la relación de etiqueta,
rangos, escrituras del modelo sin eventos, el orden de eventos con su valor, la
activación por teclado de la casilla y la limpieza de señales y widgets, incluso
con un control retenido fuera de la sesión. `tests/widgets/test_widgets.py`
recorre el ejemplo por protocolo: valores iniciales, cambios que llegan al modelo,
restablecer, rechazo de valores no válidos y una captura con todos los controles.

## Próximas entregas

Siguen pendientes los otros grupos pedidos: indicadores (progreso, spinner,
imagen, separador), contenedores (grid, marco, expansor, pestañas, panel
dividido), texto multilínea, menús y barra de título, controles dentro de
regiones dinámicas, y la integración con la bandeja y las notificaciones del
escritorio mediante tarn-dbus.

## Indicadores

Tres indicadores de solo lectura usan el mismo camino que los controles.
`progress` es una `GtkProgressBar` con porcentaje, `level` una `GtkLevelBar`
continua y `spinner` un `GtkSpinner`. El valor sale del modelo y se actualiza
en `View.inputs`. No generan eventos: `ui.fill` y `ui.click` los rechazan con
`WRONG_ELEMENT_TYPE`.

```tarn
children.push(components.progress("volume-meter", "Nivel de volumen", volume, 0, 100))
children.push(components.level("copies-meter", "Copias en cola", copies, 1, 99))
children.push(components.spinner("syncing", "Sincronizando avisos", busy))
```

Un spinner detenido no se dibuja, y la suite lo captura así porque desactiva
las animaciones.

## Contenedores

Tres contenedores nativos agrupan nodos en las páginas estáticas:

- **Marco** (`frame`): un `GtkFrame` con título.
- **Desplegable** (`expander`): un `GtkExpander`. Si está abierto o cerrado es
  estado del modelo, igual que un control. Con una acción distinta de cero, cada
  cambio del usuario llega con `1` o `0` como texto del evento.
- **Pestañas** (`tabs` y `tab`): un `GtkNotebook`. El índice seleccionado es
  estado del modelo, y `tabs` solo acepta hijos `tab`.

```tarn
children.push(components.frame("meters", "Indicadores", meters))
children.push(components.expander("details", "Detalles", open, 7, details))
pages.push(components.tab("tab-general", "General", general))
pages.push(components.tab("tab-about", "Acerca de", about))
children.push(components.tabs("sections", selected, 8, pages))
```

Los hijos usan la clave del contenedor como padre. GTK 4 desconecta el
contenido de un desplegable cerrado, así que no aparece en `ui.inspect` hasta
que se abre. Por ahora los contenedores no se pueden usar dentro de regiones
dinámicas. Para las pestañas se eligió `GtkNotebook` y no
`GtkStackSwitcher`, porque este emite advertencias de *baseline* con
pestañas sin icono.

## Texto multilínea

`text_area` es un `GtkTextView` con ajuste de línea dentro de un marco con
desplazamiento. `lines`, de 2 a 20, fija la altura visible. Funciona como un
campo de texto: cada edición llega como acción con el texto completo (hasta
4096 caracteres) y el modelo lo vuelve a escribir sin generar eventos. Tab
sigue moviendo el foco. `ui.fill` acepta saltos de línea solo en este widget.

```tarn
children.push(components.text_area("note", settings.note.clone(), 4, 9))
```

## Barra de título y menú

`Header` reemplaza la barra de título por una `GtkHeaderBar` con subtítulo,
botones al inicio o al final y un menú principal. Un botón con icono usa su
texto como tooltip y nombre accesible. Los botones son componentes con clave,
así que `set_text` y `set_enabled` les aplican; por eso el `Header` se monta
después de la primera página.

Las entradas del menú son acciones de ventana `tarn.<clave>`, con claves de
letras ASCII, dígitos y `-`. Las herramientas de desarrollo las activan por
clave con `ui.click`, porque no existen como widgets hasta que se abre el menú.

```tarn
var buttons: Vec<components.HeaderButton> = Vec.new()
buttons.push(components.header_button("header-reset", "Restablecer", "edit-undo-symbolic", 6, false))
var menu: Vec<components.MenuItem> = Vec.new()
menu.push(components.menu_item("menu-about", "Ir a «Acerca de»", 10))
try components.Header{subtitle: "Ajustes nativos", buttons: buttons, menu: menu}.mount(ui)
```

GTK solo acepta una barra de título en una ventana sin realizar. `set_header`
oculta y desrealiza la ventana un instante, así que conviene llamarlo
mientras se construye la interfaz, antes del primer evento.

## En regiones dinámicas

Los controles, los indicadores, los marcos, los desplegables y el texto
multilínea también se pueden usar en regiones dinámicas (`View.regions`), con
la misma API que en las páginas estáticas. En cada reconciliación:

- Una clave nueva se crea con el mismo constructor que en una página estática.
- Una clave existente se reutiliza. Debe conservar su tipo, su padre, su
  acción y su rango; el valor y el texto se reescriben sin generar eventos.
- Las claves que desaparecen se retiran junto con sus manejadores, también
  dentro de un desplegable cerrado.

Las pestañas siguen siendo solo estáticas. El ejemplo de widgets monta un
bloque «Avanzado» al activar «Opciones avanzadas». La prueba lo enciende, lo
edita, lo apaga y lo vuelve a montar desde el modelo, y una sonda con
AddressSanitizer repite el ciclo 25 veces sin errores.

## Advertencia conocida con WhiteSur

Con el tema WhiteSur, `GtkSwitch` hace que GTK imprima «GtkImage reported
baselines…» en stderr. Lo provocan los iconos internos del interruptor con
ese tema; no afecta al funcionamiento. Las pruebas fijan Adwaita y una
configuración aislada, así que no lo ven.
