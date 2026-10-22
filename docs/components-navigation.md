# Componentes con claves y navegación

El ejemplo de `examples/navigation/main.tarn` tiene dos pantallas GTK: contador
y resumen. Se puede incrementar desde cualquiera, abrir el resumen y volver
conservando el valor. La paridad del resumen se deriva del mismo estado Tarn.

```bash
bash scripts/build-navigation.sh
bash scripts/run-navigation.sh
```

Se requiere el SDK GTK4 y el compilador suministrado en
`../tarn/target/debug/tarn`, o un `TARN_BIN` explícito. `--dev` compila aparte en
`.build/navigation-dev`, con el canal opcional y sus permisos habituales.
La compilación normal no incluye ese canal. La ABI nativa vigente es **6**;
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
ni reinicia su contenido. No hay reconciliación de árboles, desmontaje dinámico,
contenedores arbitrariamente anidados ni ejecución asíncrona de efectos en esta
entrega. El montaje queda cerrado después de la primera llamada a `show_page`.

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
  `main`, `heading`, `content` y `pages`. No se aceptan duplicados.
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
- Ocho pruebas de integración: estado compartido, rechazo de botones ocultos,
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
