# Bloques condicionales y subárboles mixtos

`View.regions` amplía las regiones dinámicas a etiquetas, encabezados, valores,
botones, campos de texto, filas y columnas anidadas. Las páginas siguen montadas
una sola vez; cada región ocupa una caja estática inicialmente vacía.
La ABI vigente es **14**: recompilar aplicación y biblioteca juntas.

La demo incluye **Abrir mi lista → Mostrar u ocultar opciones**. Al abrir monta
un encabezado, un campo de nota accesible, una vista previa y una fila de botones.
Cerrar retira el grupo completo; volver a abrirlo recupera la nota del modelo.
Vaciar nota actualiza el campo desde Tarn sin producir otro evento de edición.

```bash
bash scripts/build-navigation.sh
bash scripts/run-navigation.sh
bash scripts/test-navigation.sh
```

## Descripción y propiedad

Una región usa los mismos `components.Node` que el montaje estático:

```tarn
var children: Vec<components.Node> = Vec.new()
children.push(components.label("summary", "Opciones"))
if expanded {
    var fields: Vec<components.Node> = Vec.new()
    fields.push(components.field("note", note.clone(), 15,
        "Nota", "El texto se conserva al cerrar las opciones."))
    fields.push(components.button("close-options", "Cerrar", 17))
    children.push(components.column("options-body", 8, fields))
}
var regions: Vec<components.Region> = Vec.new()
regions.push(components.Region{key: "options", children: children})
```

El contenedor `options` debe haberse montado vacío. La vista incluye
`regions: regions` junto a `route`, `texts`, `enabled`, `inputs` y `lists`.
Las vistas sin regiones mixtas deben usar `regions: Vec.new()`.
Omitir una región conserva su contenido; describirla con hijos vacíos la vacía.
Las listas de botones `View.lists` siguen disponibles como alternativa específica.
Una caja no puede pertenecer a ambos mecanismos ni incluir hijos estáticos.

El callback devuelve descripciones propias. C recorre padres antes que hijos,
conserva el widget por clave y lo reordena dentro de su padre, actualiza sus
propiedades y crea los controles nuevos. Al terminar retira las ramas ausentes.
Cambiar las condiciones en Tarn decide qué ramas existen; no hay un lenguaje de
plantillas adicional ni callbacks Tarn invocados desde C.

`components.field(key, text, action, accessible_label, description)` añade un
campo con propiedades accesibles explícitas tanto en páginas estáticas como en
regiones. `input` continúa disponible. Los encabezados usan el rol y nivel de
GTK; los botones primarios usan su clase semántica nativa. La lectura del texto
y de las propiedades accesibles explícitas sigue requiriendo `allow_text`.

## Identidad y estado

Una clave que continúa presente conserva tipo, padre e ID de acción. Cambiar
cualquiera de esos tres atributos devuelve error; debe usarse otra clave para
representar otra identidad. El texto, el espacio entre hijos de un grupo y el
orden de hermanos pueden cambiar manteniendo los objetos nativos.

La reconciliación visita los nodos de las regiones descritas en cada vista;
los setters nativos omiten valores iguales y los movimientos omiten posiciones
iguales. Los campos sincronizan texto desde el modelo sin encolar una edición.
Cuando el contenido ya coincide, conservan el cursor. Los nombres y descripciones
accesibles iguales tampoco vuelven a aplicarse.

Una rama ausente se desmonta con todos sus descendientes: desconecta señales de
botones y campos, retira las claves y libera la jerarquía GTK. Al reaparecer crea
controles nuevos. Para conservar datos, mantenerlos en el modelo Tarn como hace
la nota de la demo; no depender del estado interno de un control desmontado.

Antes de desmontar se limpian los punteros de foco guardados de todas las páginas.
Si el foco estaba en la rama retirada, se intenta enfocar el primer control
disponible de la página visible. `Session.focus` admite campos GTK compuestos,
que delegan el foco en un hijo interno. Los controles externos a la rama siguen
conservando sus objetos y su identidad.

Las acciones ya encoladas antes del desmontaje siguen siendo valores escalares.
El modelo debe decidir cómo tratar acciones de elementos que dejaron de existir;
para elementos distintos no debe reutilizar IDs como si fueran la misma entidad.
Los handlers desconectados no generan nuevas acciones, aunque otro propietario
nativo todavía conserve una referencia al control retirado.

## Límites y fallos

- Hasta 12 niveles de grupos por región y 256 nodos por descripción de región.
- Hasta 256 claves registradas al terminar cada región, compartidas con páginas,
  componentes estáticos y listas. El reemplazo puede conservar temporalmente
  ambas generaciones, limitado a 512 claves.
- Las claves siguen siendo globales a la sesión. Las regiones y sus nodos no
  aceptan claves duplicadas. Los IDs de acción conservan el rango 1..1024.
- No se admiten regiones independientes dentro de grupos que ya pertenecen a
  otro árbol dinámico, ni movimientos de nodos entre padres o regiones.
- Un fallo parcial no hace rollback: devolver el error al runner para cerrar y
  liberar la sesión. La solicitud del agente no recibe confirmación de éxito.

La API nativa usa `tree_begin`, `tree_node` en preorden y `tree_end`. El conteo
incluye grupos y hojas, no hijos internos de GTK. Toda operación requiere el hilo
de la sesión; cierre, cola llena y límites conservan sus errores habituales.
La sesión libera también los registros auxiliares de un recorrido interrumpido.

## Evidencia

La suite funcional comprueba identidad de grupos, campo y botón en 20
actualizaciones con reordenamiento; verifica rol y nivel de encabezados, estilo
primario, foco guardado en páginas ocultas, actualización de campos sin eventos
extra y desconexión de señales con referencias externas a los controles retirados.
También reemplaza un subárbol completo de 254 nodos por otro al límite de capacidad,
vacía la región y verifica la limpieza de una reconciliación interrumpida.

La integración real mediante protocolo prueba nota UTF-8, vista derivada,
navegación, cierre, rechazo de controles retirados, reapertura repetida y limpieza
del campo desde el modelo. Las fixtures rechazan duplicados, colisiones, cambios
de tipo y exceso de profundidad sin confirmar éxito. Captura:
`.build/navigation-tests/options.png`; registros en el mismo directorio.
Las verificaciones de sanitizadores siguen [aplazadas](roadmap.md).
