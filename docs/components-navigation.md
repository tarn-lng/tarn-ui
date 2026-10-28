# Componentes con claves y navegación

El ejemplo de `examples/navigation/main.tarn` tiene tres pantallas GTK: contador,
resumen y [lista dinámica](dynamic-lists.md). Se puede incrementar desde cualquiera, abrir el resumen y volver
conservando el valor. La paridad del resumen se deriva del mismo estado Tarn.

```bash
bash scripts/build-navigation.sh
bash scripts/run-navigation.sh
```

Se requiere el SDK GTK4 y el compilador suministrado en
`../tarn/target/debug/tarn`, o un `TARN_BIN` explícito. `--dev` compila aparte en
`.build/navigation-dev`, con el canal opcional y sus permisos habituales.
La compilación normal no incluye ese canal. La ABI nativa vigente es **12**;
biblioteca y aplicación deben recompilarse juntas.

## Descripciones y actualización

`core/components.tarn` proporciona etiquetas, botones y páginas descritos con
valores propios. Las funciones de aplicación pueden componer estos valores;
el ejemplo reutiliza `counter_value` en ambas páginas con claves distintas.

```tarn
var children: Vec<components.Node> = Vec.new()
children.push(components.label("value", "0"))
children.push(components.button("increment", "Incrementar", 1))
page := components.Page{key: "counter-page", children: children}
try page.mount(ui)
```

Cada página usa una caja vertical nativa dentro de un `GtkStack`. El montaje
crea los controles una vez. Después, `components.drive` recibe una vista inicial
y un callback de acciones que devuelve `Result<components.View, session.Error>`.
La vista contiene una ruta y textos identificados por clave; el callback conserva
su modelo en Tarn y no recibe la sesión ni punteros nativos.

El enlace compara cada texto con la descripción anterior y solo llama al setter
si cambió. También omite el cambio de página cuando la ruta es igual. Confirma
la acción después de aplicar la vista, incluso cuando no hay cambios. Un cierre
sale sin invocar el callback. Las claves repetidas en una vista se rechazan antes
de aplicar propiedades, evitando diferencias ambiguas entre la descripción y GTK.

Los textos son parches de propiedades: omitir una clave no elimina su widget
ni reinicia su contenido. Las filas de `View.lists` admiten reconciliación
y desmontaje dinámicos; [`View.regions`](dynamic-regions.md) permite bloques
condicionales y subárboles mixtos dentro de cajas reservadas. No hay ejecución
asíncrona de efectos en esta entrega. El montaje queda cerrado después de la primera llamada a `show_page`.

## Rutas y ciclo de vida

`core/navigation.tarn` mantiene rutas permitidas y una pila de historial propias.
`push` rechaza rutas desconocidas y no duplica la ruta actual. `back` devuelve
false en la raíz; no vacía la pila. Cambiar de página conserva sus widgets y el
estado de aplicación. Ocultar una página no equivale a desmontarla: toda su
jerarquía se libera al terminar la sesión scoped.

La tabla de claves pertenece a la sesión y solo toma prestados widgets del árbol
GTK. Al cerrar se desconectan los handlers de todos los botones, se destruye el
árbol y se libera la tabla. No hay registro global de componentes ni closures
Tarn pasadas como callbacks C. Las señales nativas solo encolan valores.

Un fallo de montaje o de presentación no hace rollback. El patrón con `try`
devuelve el error al runner, que libera también un montaje parcial. Si un cambio
de ruta falla después de modificar el modelo, la sesión termina con ese error;
no se confirma éxito al cliente de desarrollo.

Límites actuales:

- Hasta 32 páginas y 256 claves entre páginas y componentes por sesión.
- Claves ASCII alfanuméricas, guion o guion bajo, de 1 a 128 bytes. Se reservan
  `main`, `heading`, `content`, `pages` y `scroll`. No se aceptan duplicados.
- Acciones de botón de 1 a 1024, con una cola compartida de 64 eventos.
- Hasta 32 rutas permitidas y profundidad de historial 64; exceder esta última
  devuelve el error 10. El resto de entradas inválidas devuelve 1.
- Una cola desbordada devuelve 8. El cierre prevalece sobre acciones pendientes
  y las operaciones posteriores al cierre devuelven 6.

`Session.next_action` entrega `Opened`, `Closed` o `Activate(id)`. La API
`next_event` del contador continúa separada, sin añadir variantes que obliguen
a cambiar sus matches. No se deben mezclar ambos consumidores en una sesión.

## Automatización y resultados

```bash
bash scripts/test-navigation.sh
```

La prueba compila el ejemplo real, las fixtures, las pruebas de estado y el
lanzador MCP. `--build-only` omite las ventanas y la ejecución MCP.

Verificaciones realizadas:

- Programa Tarn: rutas desconocidas y duplicadas, volver desde la raíz, ruta
  repetida, límite del historial y regreso hasta la raíz.
- Ciclo de vida nativo: identidad de las mismas páginas, etiqueta y botón tras
  20 recorridos; componentes 7/7 y handlers 4/4 liberados en esa comparación.
  También comprueba montaje parcial, límites de páginas/componentes, orden de
  acciones diferentes, acceso desde otro hilo, UTF-8, duplicados y cierre con
  cola llena. La limpieza se verifica después de cada sesión de límites.
- Veintidós pruebas de integración: estado compartido, rechazo de botones ocultos,
  navegación repetida, capturas reales de ambas páginas con hash independiente,
  pipeline de acciones/cierre, permisos, vistas sin cambios, errores de vista
  con limpieza y navegación real a través de MCP. Algunos casos agrupan varios
  escenarios dentro de una misma prueba.
- Regresión: seis pruebas reactivas y 21 del protocolo pasan. La aceptación MCP
  del contador conserva sus dos pruebas de build y seis escenarios MCP.
- Los cuatro negativos de propiedad de sesión siguen rechazados.

Registros y capturas: `.build/navigation-tests/`; compilaciones y procedencia:
`.build/navigation/` y `.build/navigation-dev/`. Las pruebas de ciclo de vida son
funcionales. La investigación de sanitizadores permanece [aplazada](roadmap.md).
La receta MCP `project.build` sigue siendo la receta fija del contador; para
automatizar navegación se configura su binario al arrancar el lanzador.

## Disposición, foco y presentación nativa

`components.row(key, spacing, children)` y `column` componen cajas GTK con
hasta 12 niveles de grupos y separación entre 0 y 64. Las claves de los grupos
comparten el límite de 256 componentes. `heading` crea un encabezado accesible
con nivel 2; `number` usa la clase nativa `title-1`; `primary_button` aplica
`suggested-action`. La aplicación respeta el tema de GTK sin instalar CSS ni
cambiar ajustes globales. Las pruebas comparan capturas Adwaita claro y oscuro.

Al entrar a una página, el foco va a su primer control enfocable. Al volver,
se restaura el último foco de esa página; actualizar textos no lo mueve.
`Session.focus(key)` permite elegir un control visible y enfocable.
`Session.set_accessible(key, label, description)` establece propiedades GTK
reales; la prueba nativa verifica rol, nivel, nombre y descripción con el
contexto de accesibilidad de prueba. Esto no certifica un lector de pantalla.

El inspector expone foco lógico dentro de la ventana, clases nativas y las
propiedades accesibles explícitas del adaptador (no un nombre calculado).
Estas propiedades respetan `allow_text`. La integración verifica restauración
del foco, geometría horizontal y redacción de metadatos.

## Entrada de texto reactiva

`components.input(key, initial_text, action)` monta un `GtkEntry` con un límite
de 256 caracteres Unicode. Los cambios nativos encolan el ID de acción y una
copia UTF-8 del texto de ese momento. `components.drive_input` entrega ambos
valores a un callback `fn(u32, string) Result<View, session.Error>`; los botones
entregan un texto vacío. El modelo conserva la cadena recibida y devuelve los
textos derivados, con el mismo diff y confirmación de eventos de `drive`.

La demo permite dar nombre al contador y mantiene un borrador al
escribir. Guardar nombre confirma el valor y actualiza el título del resumen.
Un nombre vacío o compuesto solo por espacios muestra una indicación y no se
puede enviar; editarlo conserva el último nombre confirmado.
El campo tiene nombre y descripción accesibles. Cambiar de página conserva tanto
su contenido como el estado Tarn. No se escriben entradas desde `View.texts`:
los valores de entrada se sincronizan mediante `View.inputs`.
No incluye contraseñas ni validación remota.

La cola comparte el límite de 64 eventos; un desbordamiento devuelve error 8.
Los snapshots pendientes se liberan al terminar la sesión, también si hay error.
`Session.event_text` copia el snapshot despachado a una cadena propia de Tarn;
el puntero C subyacente solo vive hasta `next` o el cierre. Las señales C nunca
llaman closures Tarn. El inspector lee el texto de GTK solo con `allow_text`.
La automatización ofrece [`ui.fill`](devtools.md#uifill) con permiso `fill`
explícito, tanto por protocolo como por MCP.

Las pruebas nativas insertan dos ediciones con acentos, verifican snapshots en
orden y limpieza tras desbordamiento; una fixture Tarn comprueba los valores
recibidos y la actualización visible a través de `drive_input`.

## Envío y controles habilitados

`core/forms.tarn` mantiene el borrador literal y el nombre confirmado en
`NameForm`. `edit` conserva los espacios mientras se escribe; `can_submit`
valida una copia sin espacios de los extremos y exige un nombre no vacío y
distinto del confirmado. `submit` vuelve a validar y devuelve false sin modificar
el nombre cuando el borrador es inválido o no hay cambios. Guardar es local a la
sesión: no escribe archivos ni envía datos a un servidor.

`View` incluye `enabled: Vec<components.Enabled>` junto a `route` y `texts`.
Cada elemento contiene `key` y `value: bool`. Es un parche de propiedades:
omitir una clave conserva su estado; los duplicados se rechazan antes de aplicar
propiedades. El enlace compara con la vista anterior y solo llama a
`Session.set_enabled` cuando cambia. Para vistas sin estos cambios, usar
`enabled: Vec.new()`. GTK recibe el estado mediante `gtk_widget_set_sensitive`;
el inspector lo expone y `ui.click` rechaza controles deshabilitados.

La demo conecta `save-name` a `form.can_submit()`. Tras guardar, el botón queda
deshabilitado hasta que exista otro nombre válido. Los borradores y el nombre
confirmado sobreviven a la navegación durante la sesión. La validación del modelo
protege también envíos repetidos independientemente del estado visual del botón.
Las pruebas cubren espacios, UTF-8, borrador frente a confirmación, envío repetido,
propiedades habilitadas duplicadas, hilo incorrecto y operaciones tras cierre.

## Restablecer y sincronización desde el modelo

`NameForm.reset()` recupera el nombre confirmado, o una cadena vacía antes del
primer guardado. `value()` devuelve el borrador literal; `can_reset()` indica si
difiere del nombre guardado. Guardar normaliza los espacios de los extremos y
sincroniza tanto el borrador como el campo visible. Restablecer descarta ediciones
sin modificar el nombre guardado. El botón queda deshabilitado sin diferencias.

`View.inputs: Vec<components.Text>` contiene parches de valores de entrada.
Debe incluirse en las descripciones (`inputs: Vec.new()` si no hay campos).
El enlace valida claves repetidas antes de aplicar y compara valores con la vista
anterior. `Session.set_input(key, text)` admite solo entradas GTK, UTF-8 válido
y hasta 256 caracteres. Si el contenido ya coincide, evita escribirlo; así las
ediciones habituales conservan el cursor. Las escrituras del modelo bloquean
solo el handler `changed` del adaptador y no encolan ediciones del usuario.
Los demás observadores GTK siguen recibiendo sus señales normales.

La prueba nativa alterna 200 escrituras del modelo antes de dos ediciones reales:
la cola contiene únicamente las dos ediciones y conserva sus snapshots UTF-8.
También rechaza tipos incorrectos, texto inválido y valores demasiado largos.
La integración comprueba restablecer antes y después de guardar, con igualdad
entre el campo visible y el valor confirmado y con Guardar deshabilitado al final.

La suite también conduce el formulario de la aplicación real mediante `ui.fill`,
sin hooks: escribe un nombre, lo guarda, navega y restablece otro borrador.
La aceptación MCP verifica el esquema de la herramienta y el título derivado.

La extensión [`View.lists`](dynamic-lists.md) añade regiones de filas dinámicas;
las descripciones sin listas usan `lists: Vec.new()`.
