# Fase 2: resultados medidos

Ejecución: 20 de octubre de 2026, Linux x86_64, GTK 4.24.1, Pango 1.58.2,
Fontconfig 2.18.3 y GCC 16.2.1. Se usó el binario Tarn construido previamente
desde `da3173c4f012ead629aa03e67d3d9147615fec62`; no se reconstruyó ni modificó
el compilador durante esta tarea. El repositorio vecino recibió cambios externos:
se preservaron y se verificó por separado una copia limpia de ese commit.

## Pruebas ejecutables

| Comprobación | Resultado real |
| --- | --- |
| Compilación del ejemplo y puente ABI 2 | Salida 0; diagnósticos JSON vacíos |
| Activación GTK con `gtk_widget_activate` y siete señales `clicked` adicionales | Valor inicial 0, resultado 8; cada evento cambia una vez el estado y la etiqueta |
| Cola de clics antes de consumirlos | La etiqueta conserva su valor; el callback no incrementa el contador |
| Cierre con tres clics pendientes | Se entrega Closed, se descartan clics y una actualización posterior falla con 6 sin cambiar el estado |
| `i64::MAX - 1` y dos clics | El primero alcanza MAX; el segundo devuelve error 9, sin wraparound ni aborto |
| `i64::MIN` | Etiqueta decimal correcta e incremento a MIN + 1 |
| Error de cuerpo propagado con `try` | Código 99 preservado y sesión liberada |
| Retorno temprano con eventos pendientes | Árbol GTK, señales y almacenamiento liberados |
| Límite de cola | 64 eventos se extraen; 65 sin consumir producen error 8; cerrar tiene prioridad incluso sobre ese error |
| Hilo incorrecto | Creación, lectura, actualización, habilitación, cierre y liberación rechazados antes de operar sobre GTK |
| Sesión anidada y habilitación duplicada | Rechazadas |
| Fallos parciales de creación | Tres etapas anteriores y fallo después de montar/conectar el contador liberan sus recursos |
| Repetición de 16 ciclos nativos | Aserciones de ciclo de vida completadas |
| Regresión del runner sin contador | Cierre normal, retorno temprano y error; salida 0 |

La integración real en Tarn (`tarn-smoke.log`) terminó con **0** y emitió seis
resultados PASS. La regresión del runner (`tarn-session-smoke.log`) también terminó
con **0**. Los tests nativos completaron sus aserciones y después LSan hizo fallar
sus procesos con **1**; no se presenta ese comando agregado como aprobado.

Contabilidad nativa final, mediante contadores explícitos y notificaciones débiles
de finalización de GObject:

| Recurso | Creado / liberado o finalizado |
| --- | --- |
| Sesiones | 20 / 20 |
| Ventanas | 19 / 19 |
| Encabezados | 18 / 18 |
| Contenedor, etiqueta numérica y botón del contador | 51 / 51 |
| Conexiones de señales | 51 / 51 |

La captura real del widget GTK se inspeccionó: 480 × 240 con encabezado, valor
inicial 0 y botón Incrementar. La biblioteca de producción exporta únicamente
las siete funciones `tarn_ui_gtk_*`; no contiene los hooks de pruebas ni un canal
de control. Los temporizadores de vigilancia pertenecen a las pruebas.

## Compilación inválida real

Las cuatro pruebas de la API actual se compilan con el compilador real y deben
fallar con un diagnóstico específico. Sus binarios no se ejecutan:

| Caso | Diagnóstico observado |
| --- | --- |
| Construcción del campo privado | E3014 |
| Transferir sesión prestada a una tarea | E3047 |
| Retener el parámetro de la sesión fuera del cuerpo | E4210 |
| Dos préstamos mutables simultáneos | E4101 |

También se ejecutaron las reproducciones reubicadas en `tests/compiler`:
las tres pruebas de sesión rechazaron los programas inválidos; las pruebas
recuperadas de callback y almacenamiento rechazaron los préstamos con E4210 y
E4207; el control válido compiló y mostró `42`; la sesión positiva produjo
`true`, `closed`, `ok`, `closed`, `7`, `1`.

Antes de recuperar esas tres pruebas, con autorización del usuario se guardó
su contenido en `.build/backups/borrow-effects-before-repair`. Dos habían dejado
de retener préstamos y una tenía sintaxis inválida. Se restauraron casos
negativos que fallan por préstamos, no por sintaxis; no se ejecutó código inválido.

## Investigación separada de LeakSanitizer

Todas las ejecuciones siguientes mantuvieron `detect_leaks=1`, ASan y UBSan.
No se añadieron supresiones, exclusiones ni limpieza global de dependencias para
forzar un resultado verde.

| Proceso | Bytes pendientes | Asignaciones pendientes | Salida |
| --- | ---: | ---: | ---: |
| Puente, fallos parciales y 16 ciclos; renderizador predeterminado | 633339 | 13870 | 1 |
| GTK sin el puente; 16 ventanas; predeterminado | 633339 | 13870 | 1 |
| GTK sin el puente; 1 ventana; predeterminado | 633339 | 13870 | 1 |
| GTK sin el puente; 16 ventanas; predeterminado, investigación separada | 633339 | 13870 | 1 |
| GTK sin el puente; 1 ventana; Cairo | 576603 | 13594 | 1 |
| GTK sin el puente; 16 ventanas; Cairo | 576603 | 13594 | 1 |

Las trazas del renderizador predeterminado incluyen Fontconfig y Mesa/EGL. Con
Cairo quedan trazas de Fontconfig/Pango y desaparece el exceso de 56736 bytes del
caso predeterminado. Los tamaños no crecen entre una y dieciséis ventanas en esta
muestra y no hay bytes adicionales al usar el puente en la comparación medida.

Esto apoya la hipótesis de retención de las dependencias a nivel del proceso,
separada del ciclo de vida de las sesiones. No demuestra que todas las
asignaciones nativas estén libres de fugas: **el fallo global de LSan sigue
abierto**. No aparecieron informes de acceso inválido de ASan ni de comportamiento
indefinido de UBSan en estos registros.

Se consultó la [documentación oficial de PangoCairo](https://docs.gtk.org/PangoCairo/method.FontMap.set_default.html):
su mapa de fuentes predeterminado es local al hilo y puede liberarse al cambiarlo.
No se añadió esa limpieza global al cierre de cada sesión: requeriría verificar
el resto de consumidores y no explicaría por sí sola todas las trazas observadas.

## Reproducción y archivos

```bash
TARN_SOURCE="$PWD/.build/tarn-source" bash scripts/build-counter.sh
bash scripts/test-gtk.sh --build-only
bash scripts/test-gtk.sh
bash scripts/investigate-gtk-leaks.sh
```

La segunda orden sale 0. Las dos últimas salen **1** por los hallazgos descritos.
Registros, códigos de salida, diagnósticos y capturas están en
`.build/evidence/phase2` y `.build/evidence/leak-investigation`; la procedencia
de producción está en `.build/counter/provenance.txt`. Están excluidos de Git.

No se repitió la suite completa. No se implementaron MCP, componentes reactivos,
navegación ni plataformas móviles. No hubo `git add`, commit ni publicación.
