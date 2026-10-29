# Listas dinámicas con claves estables

La demo de navegación incorpora **Mi lista**: permite escribir una fila, añadirla,
seleccionarla, editar su texto, subirla y eliminarla. Al volver a otra pantalla y
regresar conserva las filas y el borrador. Las páginas siguen montándose una vez;
las filas se reconcilian dentro de una región dinámica reservada.

```bash
bash scripts/build-navigation.sh
bash scripts/run-navigation.sh
bash scripts/test-navigation.sh
```

La ABI vigente es **13**. Recompilar aplicación y biblioteca juntas.

## Descripción en Tarn

Montar una columna vacía con una clave estable y describir sus filas en
`View.lists`. Cada `components.List` contiene la clave del contenedor y una
`Vec<components.Button>` con clave, texto y acción de cada fila:

```tarn
var rows: Vec<components.Button> = Vec.new()
rows.push(components.Button{key: "item-42", text: "Revisar propuesta", action: 142})
var lists: Vec<components.List> = Vec.new()
lists.push(components.List{key: "items", items: rows})
return components.View{route: "home", texts: Vec.new(), enabled: Vec.new(),
    inputs: Vec.new(), lists: lists, regions: Vec.new()}
```

Las vistas existentes requieren `lists: Vec.new()` si no contienen listas.
Omitir una región conserva su contenido; describirla con `items: Vec.new()` la
vacía. Las listas sin diferencias de clave, orden, texto y acción no invocan
reconciliación nativa. Las filas usan botones GTK reales, con su rol accesible
nativo; su etiqueta puede cambiar manteniendo la misma identidad.

La primera reconciliación reserva un `GtkBox` vacío para uso exclusivo de la
lista. No admite un contenedor con hijos estáticos. Los nombres de las filas
comparten el registro global de claves de la sesión. No se pueden mover filas
entre regiones ni cambiar la acción asociada a una clave existente.

## Identidad y eliminación

Para cada descripción, GTK conserva el botón cuya clave ya existe, modifica su
texto si cambió y lo coloca en el orden pedido sin desmontarlo. Crea los botones
nuevos y retira los ausentes al terminar la región. Antes de retirar una fila,
desconecta su señal y borra las referencias de foco guardadas que la señalen.
Si se elimina el control enfocado, intenta enfocar el primer control disponible
de la página visible. El registro deja de resolver la clave retirada; el
inspector y `ui.click` trabajan sobre el árbol actual.

La señal C solo encola la acción escalar. Una acción que ya estaba encolada antes
de retirar la fila puede entregarse después: el modelo debe ignorar IDs ausentes
y evitar reutilizarlos para otro elemento. `examples/navigation/tasks.tarn` usa
IDs crecientes y demuestra ese tratamiento. Las claves estables representan la
identidad del dato, no su posición en el vector.

La clave que expone un botón dinámico al inspector pertenece al propio objeto
GTK. Así sigue siendo válida aunque otra referencia nativa conserve el botón
después de retirarlo del registro. El cierre scoped libera también una
reconciliación interrumpida y sus estructuras auxiliares.

## Límites y errores

- Máximo de 256 claves registradas al terminar una región, compartidas con las
  páginas y componentes estáticos. Las filas consumen una clave cada una.
- Las claves conservan el formato ASCII y las acciones el rango 1..1024.
- El recambio completo puede conservar temporalmente ambas generaciones, con
  un límite interno de 512 claves durante la operación.
- El enlace valida regiones e ítems duplicados antes de aplicar la vista.
  El adaptador rechaza colisiones con otras regiones o componentes, acciones
  cambiadas para una clave existente y conteos inconsistentes.
- No hay rollback: si falla la aplicación parcial de una vista, devolver el error
  al runner para cerrar y liberar la sesión. No se confirma éxito al agente.
- La demo limita su modelo a 32 filas y 900 IDs creados por sesión. No reutiliza
  IDs retirados ni permite que el contador de acciones desborde.

Esta primera implementación reconcilia filas seleccionables dentro de cajas
reservadas. Para subárboles mixtos y bloques condicionales, usar
[`View.regions`](dynamic-regions.md). No admite cambios de tipo por clave ni
listas virtualizadas. Las páginas y los demás
grupos mantienen su topología fija. No introduce un renderizador propio.

## Verificación

La suite incluye el modelo Tarn y pruebas nativas que comprueban identidad de
punteros y orden alternado durante 20 cambios, edición de texto, eliminación de
la fila con foco guardado en una página oculta y desconexión de la señal incluso
con una referencia externa al botón eliminado. Verifica la lista vacía, la
limpieza de una reconciliación parcial y el reemplazo de 254 filas por otras 254
con el registro completo de 256 claves.

La integración conduce la aplicación real mediante `ui.fill`, `ui.click` y
`ui.inspect`: añade, selecciona, edita, reordena, navega, elimina hasta vaciar y
vuelve a añadir con una clave nueva. Las capturas y registros quedan en
`.build/navigation-tests/`, incluida `tasks.png`. Se comprueba limpieza funcional
con referencias débiles y contadores; la investigación de sanitizadores continúa
aplazada conforme a [roadmap.md](roadmap.md).
