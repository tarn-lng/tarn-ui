# Revisión de cierre de la fase 5

Revisado el 22 de octubre de 2026 sobre Linux/GTK4, ABI 12.
**Resultado: la fase 5 sigue abierta por dos fallos comprobados de disposición.**
La funcionalidad está implementada dentro del alcance actual, pero pasar los
flujos de la demo no demuestra que la interfaz completa quepa en pantalla.

Esta revisión usa los criterios de la [tabla de fases del ADR](adr/0001-native-ui-foundations.md#milestones-and-executable-gates):
interfaz declarativa/reactiva, disposición nativa, navegación, tema y accesibilidad;
identidad estable, disposición de recursos y pruebas de accesibilidad nativa,
sin renderizador propio. La investigación de sanitizadores continúa aplazada
por instrucción del usuario; no se reabre como parte de esta revisión.

## Matriz de criterios

| Criterio | Resultado | Evidencia y alcance |
| --- | --- | --- |
| Descripciones declarativas y estado Tarn | Cumplido en el alcance actual | `core/reactive.tarn`, `core/components.tarn`; modelos de formularios y listas; pruebas de valores derivados y errores de vista. |
| Reconciliación e identidad estable | Cumplido en el alcance actual | Pruebas nativas conservan punteros en listas y subárboles mixtos durante 20 actualizaciones y reordenamientos. Tipo, padre y acción son fijos por clave. |
| Liberación y señales | Cumplido funcionalmente | Referencias débiles y contadores equilibrados al cerrar, retirar ramas, reemplazar al límite y terminar montajes parciales. Los controles retirados con referencias externas no emiten acciones del adaptador. No equivale a un resultado limpio de LeakSanitizer. |
| Disposición nativa | **Pendiente: fallos reproducidos** | Las cajas y controles son GTK; las filas cortas tienen geometría comprobada. La lista completa y el texto máximo exceden el monitor, sin desplazamiento. |
| Navegación y estado | Cumplido en el alcance actual | Historial acotado, regreso, páginas retenidas, borradores conservados y recuperación desde Tarn al remontar bloques condicionales. |
| Tema y representación de capturas | Parcial en cobertura | GTK usa clases semánticas sin CSS propio ni cambios globales. Se capturó el bloque mixto con tres filas en Adwaita claro y oscuro. Hashes diferentes no prueban legibilidad. Las capturas revisadas tienen transparencia y dimensiones distintas de la ventana; su fidelidad debe contrastarse con la superficie visible. Tampoco se probaron otros temas, escalas o preferencias de contraste. |
| Accesibilidad nativa | Parcial en cobertura | Se verifican roles y niveles de encabezados y propiedades reales de botones mediante el contexto de prueba GTK. Los campos dinámicos exponen sus propiedades explícitas en el inspector. Faltan pruebas directas de sus propiedades en el contexto GTK y del recorrido de teclado. No hay validación con lector de pantalla. |
| Foco | Cumplido para los casos probados | Foco por página, delegación de campos compuestos, eliminación de referencias a ramas retiradas y conservación del foco al editar. No hay prueba completa de Tab/Shift+Tab ni activación por teclado. |
| Renderizador nativo | Cumplido | El adaptador crea `GtkBox`, `GtkStack`, `GtkButton`, `GtkEntry` y `GtkLabel`. No introduce un motor de dibujo, layout o web propio. |
| Compatibilidad con el agente | Cumplido en los flujos probados | Inspección, clic, escritura y captura con permisos explícitos; las respuestas esperan la aplicación de la vista. La aceptación de protocolo/MCP de la entrega precedente pasó; esta revisión repite navegación e integración MCP de la demo mediante su suite. |

## Medición de disposición

La auditoría obtuvo la geometría real del monitor mediante GDK: **1366×768
píxeles lógicos, escala 1**. Las dimensiones de ventana vienen de `ui.inspect`
tras una captura de un frame real. No se infiere el tamaño a partir del PNG.

| Escenario | Ventana medida | Resultado |
| --- | --- | --- |
| Tres filas y opciones abiertas, Adwaita claro | 480×661 | Cabe en el monitor medido. |
| Tres filas y opciones abiertas, Adwaita oscuro | 480×661 | Cabe; se guardó una captura independiente. |
| 32 filas y opciones abiertas | **480×1589** | Excede la altura de todos los monitores disponibles. Volver queda en `y=1539`, fuera de los 768 píxeles del monitor. |
| Una fila con 256 letras W | **3249×472** | Excede el ancho de todos los monitores disponibles. La entrada acepta este contenido válido. |

El árbol inspeccionado contiene **cero `GtkScrolledWindow`** en los cuatro casos.
El código de la página usa cajas sin un área de desplazamiento; los botones de
fila se crean con etiquetas sin política de ajuste de texto. Esto explica los
mínimos de tamaño observados. Una captura completa o un clic semántico exitoso
no prueba que el control sea alcanzable por una persona en su pantalla.

## Brechas y orden de trabajo

1. **Corregir el crecimiento vertical.** Incorporar desplazamiento nativo y una
   política de tamaño que mantenga disponibles los controles con 32 filas y las
   opciones abiertas. Agregar una prueba de geometría en un tamaño acotado y de
   acceso al final de la lista, conservando identidad y foco al desplazarse.
2. **Corregir las etiquetas largas.** Definir ajuste o elipsis nativos para las
   filas y los controles dinámicos; una cadena válida de 256 caracteres no debe
   imponer una ventana más ancha que el monitor. Verificar también texto Unicode.
3. **Completar cobertura de teclado y accesibilidad dinámica.** Probar
   Tab/Shift+Tab, activación nativa por teclado, retiro de la rama enfocada y las
   propiedades accesibles reales de sus campos. Los fallos de teclado todavía
   no están demostrados: lo pendiente es la evidencia, no una regresión afirmada.
4. **Verificar fidelidad de captura y legibilidad con los temas.** Las capturas
   cortas miden 440×610 frente a una ventana inspeccionada de 480×661 y contienen
   transparencia. `dev_capture` renderiza un nodo sin viewport explícito; el
   recorte podría depender de sus límites. Falta comprobar el contrato de
   coordenadas y composición con la superficie visible. No se afirma que el tema
   de la ventana real esté roto a partir de un PNG transparente.
5. Repetir los escenarios de disposición y revisar esta matriz antes de cerrar
   la fase. Una comprobación manual con lector de pantalla es recomendable antes
   de presentar la preview como accesible; no se declara una certificación.

Las dos primeras brechas son fallos de comportamiento medidos. La tercera y la cuarta son
cobertura pendiente. No se exige introducir virtualización, cambios de tipo por
clave, movimientos entre padres, trabajo asíncrono ni un backend móvil para
corregir estos casos. Tampoco se cambia el alcance hacia la plantilla de proyecto.

## Reproducción y registros

```bash
bash scripts/audit-phase5.sh
```

Requiere el compilador suministrado, GTK4/JSON-GLib y una sesión gráfica. La receta
recompila y ejecuta las suites de navegación y reactividad, y después mide los
cuatro escenarios. En esta revisión pasaron **22 pruebas de integración** de
navegación y **6 reactivas**, además de las pruebas nativas y modelos Tarn que
incluyen sus scripts. No se ejecuta la investigación de fugas ni Cargo completo.

Los resultados están en `.build/evidence/phase5-audit/`:

- `layout.json`: dimensiones, temas, hashes de capturas, hallazgos y
  `layout_gate_passed`. Un valor false conserva el fallo de disposición. Incluye las dimensiones PNG
  comprobadas contra la respuesta y si coinciden con la asignación de la ventana.
- `short-light.png`, `short-dark.png`, `full-list.png`, `long-label.png`, con sus
  snapshots `*.inspect.json` correspondientes.
- `monitors.json`: geometría GDK; `navigation.log`, `reactive.log`, `lifecycle.log`,
  `tasks.log` y `forms.log`: resultados de pruebas.
- `provenance.txt` y `build-provenance.txt`: versiones y hashes de fuentes y build.

La receta termina correctamente si puede recoger la evidencia; **su código de
salida cero no declara cerrado el criterio de disposición**. El resultado del
criterio está en `layout_gate_passed` y los hallazgos quedan explícitos.

Los resultados de protocolo y MCP más recientes corresponden a la entrega de
regiones mixtas: 21 pruebas del protocolo, dos verificaciones de build y seis
escenarios MCP, además de los cuatro negativos de propiedad de sesión. Se
conservan como regresión de esa entrega; esta auditoría no vuelve a declararlos
como una ejecución nueva.

## Aplazado por decisión del usuario

La investigación Mesa/EGL y el baseline de sanitizadores siguen en
[memory-status.md](memory-status.md), sin supresiones nuevas ni instalación del
parche candidato. La evidencia funcional de liberación se mantiene separada de
ese asunto. La fase 6 sigue siendo preparación móvil y no se inició aquí.
