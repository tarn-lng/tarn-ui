# Revisión de cierre de la fase 5

Revisado el 28 de octubre de 2026 sobre Linux/GTK4, ABI 12.
**Resultado: los criterios de la fase 5 se cumplen en el alcance medido.** Las dos
brechas de disposición del 22 de octubre están corregidas y las coberturas de
teclado, accesibilidad dinámica, captura y temas tienen pruebas propias.
La revisión anterior se conserva en el historial de Git.

Esta revisión usa los criterios de la [tabla de fases del ADR](adr/0001-native-ui-foundations.md#milestones-and-executable-gates):
interfaz declarativa/reactiva, disposición nativa, navegación, tema y accesibilidad;
identidad estable, disposición de recursos y pruebas de accesibilidad nativa,
sin renderizador propio. La investigación de sanitizadores sigue aplazada
por instrucción del usuario y no forma parte de esta revisión.

## Matriz de criterios

| Criterio | Resultado | Evidencia y alcance |
| --- | --- | --- |
| Descripciones declarativas y estado Tarn | Cumplido en el alcance actual | `core/reactive.tarn`, `core/components.tarn`; modelos de formularios y listas; pruebas de valores derivados y errores de vista. |
| Reconciliación e identidad estable | Cumplido en el alcance actual | Las pruebas nativas conservan punteros en listas y subárboles mixtos durante 20 actualizaciones y reordenamientos. Tipo, padre y acción son fijos por clave. |
| Liberación y señales | Cumplido funcionalmente | Referencias débiles y contadores equilibrados, incluido el nuevo `GtkScrolledWindow`. No equivale a un resultado limpio de LeakSanitizer. |
| Disposición nativa | **Cumplido** | Las páginas están en un `GtkScrolledWindow` vertical. La ventana mide 480×640 en un monitor de 1366×768 con 3 o 32 filas, con 256 «W» y con 256 caracteres Unicode mixtos. Las etiquetas ajustan el texto en cualquier carácter. |
| Navegación y estado | Cumplido en el alcance actual | Historial acotado, regreso, páginas retenidas y borradores conservados. Al cambiar de página el desplazamiento vuelve arriba. |
| Tema y representación de capturas | **Cumplido en los temas medidos** | La captura usa un viewport explícito: el PNG mide exactamente lo mismo que la ventana y es opaco. Se midieron Adwaita claro y oscuro, HighContrast, HighContrastInverse y una fuente de 20 pt. El contraste mínimo del texto visible es 10.25:1. |
| Accesibilidad nativa | **Cumplido en cobertura de pruebas** | El contexto de prueba GTK verifica el rol `TEXT_BOX` y la etiqueta y descripción reales de un campo dinámico, el rol de botones y encabezados dinámicos, y la reaplicación tras remontar. No hay validación con lector de pantalla. |
| Foco y teclado | **Cumplido** | Tab recorre 32 filas, campo, botón aplicar y Volver en orden del modelo; Shift+Tab recorre en sentido inverso. Cada control enfocado queda dentro del viewport. La activación por la señal nativa `activate` entrega la acción. Retirar la rama enfocada mueve el foco a un control visible. |
| Renderizador nativo | Cumplido | El adaptador crea `GtkBox`, `GtkStack`, `GtkScrolledWindow`, `GtkButton`, `GtkEntry` y `GtkLabel`. No introduce un motor propio. |
| Compatibilidad con el agente | Cumplido en los flujos probados | Inspección, clic, escritura y captura con permisos explícitos. Se repitieron las suites de protocolo, lanzador, MCP y build. |

## Cambios

1. **Desplazamiento vertical.** `add_page` coloca el `GtkStack` dentro de un
   `GtkScrolledWindow` (horizontal nunca, vertical automático). La pila ya no
   iguala la altura de las páginas ocultas. La altura por defecto se deriva del
   primer monitor: `alto - 128`, acotada entre 320 y 720 píxeles. El
   `GtkViewport` desplaza automáticamente el control enfocado a la vista. La
   clave `scroll` queda reservada.
2. **Etiquetas largas.** Todas las etiquetas, incluidas las de botones estáticos,
   de filas y de subárboles, usan `PANGO_WRAP_WORD_CHAR`. El límite de ancho de
   una palabra desaparece, así que la ventana conserva su ancho de 480.
3. **Captura.** `dev_capture` renderiza con un viewport `0,0,ancho,alto`. Antes
   el tamaño dependía de los límites del nodo (440×610 frente a 480×661). La
   transparencia observada provenía del CSS de usuario
   (`~/.config/gtk-4.0/gtk.css`), que sustituye a `GTK_THEME`. La auditoría
   ahora usa un `XDG_CONFIG_HOME` aislado, `GDK_DEBUG=no-portals` y animaciones
   desactivadas para que inspección y captura vean la misma posición.

## Medición de disposición y tema

Monitor medido con GDK: **1366×768 píxeles lógicos, escala 1**.

| Escenario | Ventana | Contraste mínimo |
| --- | --- | ---: |
| Tres filas, opciones abiertas, Adwaita | 480×640 | 11.61 |
| Igual, Adwaita oscuro | 480×640 | 10.25 |
| Igual, HighContrast | 480×640 | 13.88 |
| Igual, HighContrastInverse | 480×640 | 11.54 |
| Igual, fuente Sans 20 (encabezado 36→73 px) | 480×640 | 11.61 |
| 32 filas, opciones abiertas | 480×640 | 11.61 |
| Una fila con 256 «W» | 480×640 | 11.61 |
| Una fila con 256 caracteres latinos, emoji, CJK y árabe | 480×640 | 11.61 |

El contraste se calcula sobre la captura: la razón entre el píxel opaco más
oscuro y el más claro dentro de cada etiqueta habilitada visible en el viewport.
Mide legibilidad renderizada; no es una auditoría WCAG ni de lector de pantalla.
El umbral del criterio es 4.5:1. Con las opciones abiertas el botón Volver queda
debajo del pliegue y se alcanza desplazando o con Tab; la prueba nativa lo comprueba.

## Límites que se mantienen

- La escala 2 no se midió: el monitor disponible es de escala 1 y Wayland ignora
  `GDK_SCALE`. Las capturas multiplican por la escala del widget.
- La altura se calcula con el primer monitor al montar la primera página; mover
  la ventana a otro monitor no la recalcula.
- La activación por teclado usa la señal `activate` que enlazan Intro y Espacio;
  no se sintetizan eventos de teclado del compositor.
- Las pruebas nativas presentan ventanas reales: la entrada del escritorio
  (puntero o teclado) durante la ejecución puede producir eventos extra. Hubo un
  fallo así en una ejecución; las 16 siguientes pasaron.
- Recomendado antes de publicar una preview accesible: una comprobación manual
  con Orca. No se declara una certificación.

## Reproducción y registros

```bash
bash scripts/audit-phase5.sh
```

Requiere el compilador suministrado, GTK4/JSON-GLib y una sesión gráfica. La
receta recompila y ejecuta las suites de navegación (22 pruebas de integración
y las pruebas nativas de `lifecycle.c`) y de reactividad (6), y después mide los
ocho escenarios. **El código de salida refleja `layout_gate_passed`.**

Los resultados están en `.build/evidence/phase5-audit/`: `layout.json` con
dimensiones, contraste por etiqueta y hallazgos; una captura PNG y un
`*.inspect.json` por escenario; `monitors.json`; registros de pruebas y
`provenance.txt`.

En esta revisión también pasaron `test-reactive`, `test-devtools`,
`test-launcher`, `test-launcher-contract`, `test-artifacts`, `test-mcp-build` y
`test-project-build`. `test-artifacts` falló una vez sin registro y pasó en dos
reejecuciones. `test-gtk` sigue saliendo con 1 únicamente por LeakSanitizer,
igual que sin estos cambios.

## Aplazado por decisión del usuario

La investigación Mesa/EGL y el baseline de sanitizadores siguen en
[memory-status.md](memory-status.md). La fase 6 sigue siendo preparación móvil
y no se inició aquí.
