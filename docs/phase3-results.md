# Fase 3: resultados reales

Fecha: 21 de octubre de 2026. Linux x86_64, GTK 4.24.1, JSON-GLib 1.10.8,
GCC 16.2.1 y Python 3.14.8. Se utilizó el compilador Tarn fijado en
`da3173c4f012ead629aa03e67d3d9147615fec62`, ya construido. No se modificó el
compilador, no se agregaron módulos de confianza y no se ejecutó la suite completa.

## Resultado final

| Comprobación | Resultado |
| --- | --- |
| Compilación normal, sin canal | Salida 0; sin JSON-GLib ni código del transporte en la biblioteca |
| Compilación de desarrollo, ABI 3 | Salida 0; JSON estricto y canal privado opcional |
| Protocolo ejecutable | **19 tests aprobados en 7,132 s; salida 0** |
| Sonda nativa del canal con ASan, UBSan y LSan | Clic → inspección → cierre comprobados; salida nativa **0**, sin cancelación forzada ni avisos en stderr |
| Regresión funcional del contador | Salida 0 del smoke Tarn y de la regresión del runner |
| Pruebas compile-fail de la API | E3014, E3047, E4210 y E4101 observados; casos inválidos no ejecutados |
| Regresión nativa con renderizado y LSan | Puente **1** y control GTK sin puente **1**; fallo pendiente conservado |

### Aceptación sobre el ejemplo real

La prueba principal lanzó `.build/counter-dev/counter`, no el fixture. Esperó
disponibilidad por el canal, autorizó la instancia e inspeccionó los getters y
el árbol GTK real. Verificó `GtkButton`, texto numérico inicial, relaciones entre
padres e hijos, visibilidad, sensibilidad y límites medidos.

Después envió tres clics, inspección, cierre e inspección posterior en una misma
secuencia, sin esperas artificiales para asentar la UI. Las respuestas conservaron
el orden y los IDs. Los clics confirmaron revisiones 1, 2 y 3; la inspección mostró
el mismo `session_id`, revisión 3 y el valor inicial más tres. El cierre se confirmó
después de la destrucción nativa; la última inspección devolvió `WINDOW_CLOSED`.
El proceso terminó con **0**, sin intervención forzada del harness.

### Cobertura adicional

- Solicitudes sin autorización y autorización vinculada a otra instancia: rechazo.
- Permisos de operación independientes y texto redactado salvo permiso explícito.
- Verificación de lectura nativa: un fixture cambia deliberadamente solo la
  etiqueta GTK a `777`; la inspección observa `777`, no el estado esperado de Tarn.
- IDs inexistentes, tipos incorrectos, IDs de hijos internos, botón deshabilitado,
  oculto y deshabilitación heredada de su contenedor.
- Métodos fuera de fase 3 (`ui.screenshot`, `ui.type`, `ui.rebuild`): rechazo explícito.
- JSON malformado, campos adicionales, claves duplicadas, UTF-8 inválido, NUL,
  tipos incorrectos, comentarios, profundidad excesiva, versión e IDs inválidos.
- Fragmentación de registros, reutilización de IDs y cierre automático del
  lanzador sin colisiones con IDs numéricos enviados por el usuario.
- Presupuesto de 1 MiB y exceso de 32 registros encolados: error terminal acotado.
- Error del manejador: `NATIVE_ERROR`, sin confirmación ficticia; salida 1 de la
  aplicación de prueba, con recursos liberados.
- Cierre durante una acción pendiente, tanto al volver al bucle como al retornar
  inmediatamente del cuerpo: `WINDOW_CLOSED`, salida 0 y limpieza verificada.
- Ausencia de confirmación y confirmación tardía: `TIMEOUT`. El segundo fixture
  bloquea deliberadamente su manejador más allá del plazo para verificar el reloj
  monotónico, incluso sin despacho del temporizador durante ese trabajo.
- IDs de widgets duplicados, permisos vacíos o desconocidos: inicio rechazado,
  salida 1, recursos de sesión y canal liberados.
- La biblioteca normal no contiene `TARN_UI_DEV_FD`, hooks de prueba ni dependencia
  JSON-GLib. Solo la variante explícita de desarrollo tiene el transporte.

En escenarios negativos que cancelan el canal —timeout, pérdida de conexión y
exceso terminal de entrada— el harness termina el proceso de forma acotada. Las
pruebas lo registran como **cancelación forzada esperada**, no como cierre nativo
limpio. La aceptación principal y las rutas normales sí comprueban salida 0 sin
esa intervención.

Los fixtures verifican `test_clean()` al terminar normalmente o por error del
cuerpo. Incluye sesiones, finalización de widgets, conexiones de señales y balance
de canales. Los hooks solo se compilan para las pruebas, nunca en los ejemplos
normal o de desarrollo distribuibles.

## Memoria: resultados separados

La sonda del canal compila el puente C con `-fsanitize=address,undefined`, precarga
el runtime correspondiente y conserva `detect_leaks=1`. En la ejecución final,
la etiqueta inspeccionada fue `1`, el cierre se confirmó después de liberar los
widgets y el proceso salió con **0**. `memory-lsan.log` quedó vacío. La
instrumentación cubre el puente C; no agrega instrumentación al código generado
por Tarn.

Es una sonda de un ciclo del protocolo, sin captura de un frame; no sustituye el
experimento de renderizado ni demuestra ausencia de toda fuga. La regresión
nativa de renderizado sigue arrojando exactamente:

```text
Puente:     633339 bytes, 13870 asignaciones; salida 1.
GTK solo:   633339 bytes, 13870 asignaciones; salida 1.
```

Ambas ejecuciones conservaron los avisos de LeakSanitizer. La contabilidad propia
del puente en esa regresión quedó en 20/20 sesiones, 19/19 ventanas, 18/18
encabezados, 51/51 widgets del contador y 51/51 conexiones de señales.
No se añadieron supresiones ni se desactivaron comprobaciones para forzar éxito.
El hallazgo global de Fontconfig/Mesa documentado en fase 2 sigue pendiente.

## Reproducir

```bash
TARN_SOURCE="$PWD/.build/tarn-source" bash scripts/build-counter.sh
TARN_SOURCE="$PWD/.build/tarn-source" bash scripts/build-counter.sh --dev
bash scripts/test-devtools.sh
bash scripts/test-devtools-memory.sh
EVIDENCE_DIR="$PWD/.build/evidence/phase3/regression" bash scripts/test-gtk.sh
```

La última orden sigue saliendo **1** por el resultado de renderizado indicado.
Los registros locales están en `.build/evidence/phase3`; las versiones y hashes
de cada variante están en su `provenance.txt`. Esos archivos generados permanecen
ignorados por Git. No se guardan snapshots de inspección en los registros de los
tests; solo se comprueban sus campos.

La [guía del canal](devtools.md) describe permisos, contrato y límites. No se
implementaron captura por protocolo, CLI/MCP de fase 4, componentes reactivos,
navegación ni backends móviles. No hubo commits ni publicación automática.
