# Investigación de memoria: 22 de octubre de 2026

El fallo global de LeakSanitizer permanece abierto. El componente Fontconfig
se aisló como un problema de visibilidad de punteros para LSan y tiene una
corrección experimental local verificada; Mesa/EGL continúa pendiente.
La comparación recompilada
con los fuentes actuales no muestra asignaciones adicionales al usar el puente.
Esto acota los casos medidos; no demuestra ausencia general de fugas.

## Resultados actuales

GTK 4.24.1, Pango 1.58.2 y Fontconfig 2.18.3. Ejecución fuera del sandbox,
con acceso al controlador gráfico. ASan, UBSan y LSan permanecieron habilitados,
sin supresiones. Todos los casos finalizaron con salida 1 por LSan.

| Caso | Predeterminado: bytes / asignaciones | Cairo: bytes / asignaciones |
| --- | ---: | ---: |
| GTK sin puente, una ventana | 633339 / 13870 | 576603 / 13594 |
| GTK sin puente, dieciséis ventanas | 633339 / 13870 | 576603 / 13594 |
| Puente, fallos parciales y dieciséis sesiones | 633339 / 13870 | 576603 / 13594 |
| Reproductor mínimo GTK con una etiqueta | 559176 / 12964 | 559176 / 12964 |
| Reproductor mínimo y liberación del mapa de fuentes | 2969 / 72 | 2969 / 72 |

El reproductor mínimo verifica mediante referencias débiles la finalización de
ventana, etiqueta y superficie. Espera al primer frame y a esas finalizaciones;
un watchdog limita la espera. No enlaza el puente ni Tarn. Los informes residuales
del caso con liberación del mapa muestran asignaciones de Fontconfig. Los
registros no contienen informes de acceso inválido de ASan ni errores de UBSan.

La [API pública de PangoCairo](https://docs.gtk.org/PangoCairo/method.FontMap.set_default.html)
permite liberar el mapa predeterminado del hilo pasando NULL. La prueba lo hace
solo al terminar el proceso. No se ha incorporado esta operación global al cierre
de cada sesión: el descenso del informe no basta para justificar que se alteren
los recursos compartidos por otros consumidores. Tampoco elimina el fallo.

## Reproducción

```bash
bash scripts/investigate-gtk-leaks.sh
```

El script compila ambos ejecutables desde los fuentes actuales con warnings
tratados como errores. Ejecuta diez casos y conserva sus salidas aunque fallen.
Sale 1 si falla cualquiera. Requiere acceso al display y al controlador gráfico;
el sandbox de esta sesión permitió dibujar pero produjo errores de Mesa y un
total distinto, por lo que esa ejecución no se usa en la tabla.

La evidencia local está en `.build/evidence/leak-investigation/`: `summary.tsv`,
versiones, hashes de binarios y fuentes, registros, códigos de salida y capturas
de los casos que capturan. `EVIDENCE_DIR` permite conservar otra ejecución en un
directorio separado. Un caso sin resumen de LSan y con salida fallida se registra
como `unknown`, nunca como cero bytes.

## Trabajo restante

Para cerrar el fallo global queda aislar el hallazgo de Mesa/EGL y validar una
solución con el renderizador predeterminado. Los resultados de Cairo con la
biblioteca candidata no convierten el baseline con bibliotecas del sistema en
un pase. No se instalaron bibliotecas ni se cambió el cierre de producción.

## Aislamiento y corrección experimental de Fontconfig

Las trazas con símbolos de Pango 1.58.2 y Fontconfig 2.18.3 identificaron los dos
bloques directos residuales: 768 y 256 bytes de arrays de elementos de `FcPattern`.
Se asignan en `FcPatternObjectInsertElt`, llamado desde `FcFontRenderPrepare`
(selección en el hilo de Pango) y `FcFontSetList` (enumeración de familias).
La instrumentación local relacionó sus direcciones con patrones aún referenciados.

Fontconfig almacena la dirección del array como una diferencia entera respecto
al patrón (`elts_offset`). El escáner conservador no reconstruye esa dirección.
`tests/gtk/fontconfig-offset.c` demuestra el efecto sin GTK, ventanas ni Tarn:
mantiene un patrón en una variable global, ejecuta una comprobación recuperable
de LSan, lee correctamente su familia, lo destruye y repite la comprobación.
Con la biblioteca del sistema, el primer control informa 304 bytes en tres
asignaciones pese a que el patrón es accesible; después de destruirlo informa
cero. No se omiten objetos ni se registran raíces adicionales para LSan.

La [corrección candidata](../patches/fontconfig-2.18.3-lsan-reachability.patch)
conserva punteros reales en patrones del heap y desplazamientos relativos en
patrones constantes de caché. No añade liberaciones ni cambia la política de
retención: corrige la visibilidad del grafo para el escáner.

| Validación con la candidata local | Resultado |
| --- | --- |
| Patrón accesible / destruido, comprobación recuperable | 0 / 0 |
| 15 tests disponibles de Fontconfig, fuentes externas deshabilitadas | 15 pasan, 0 fallan |
| Caché con una fuente TrueType aislada, escritor candidato → lector del sistema y viceversa | Ambos coinciden, sin reescritura al leer |
| Reproductor GTK mínimo, con y sin liberar el mapa | Salida 0, ambos renderizadores |
| Control GTK, una y dieciséis ventanas, Cairo | Salida 0, sin informe LSan |
| Puente, fallos parciales y dieciséis sesiones, Cairo | Salida 0, sin informe LSan |
| Control GTK y puente, predeterminado | Salida 1, 56736 bytes / 276 asignaciones de Mesa/EGL |

La matriz usa GTK y Pango del sistema y únicamente selecciona Fontconfig local
mediante `LD_LIBRARY_PATH`. Mantiene ASan, UBSan y LSan; no tiene supresiones.
La evidencia de esa matriz está en
`.build/evidence/leak-investigation/fontconfig-pointer/`. El script ahora registra
también las bibliotecas enlazadas y el hash de Fontconfig para distinguir corridas.
La prueba de alcance y caché deja registros en `.build/fontconfig-reachability/`;
las trazas de símbolos e instrumentación, en `.build/evidence/leak-investigation/`.
Las 15 pruebas de la dependencia están en
`.build/leak-investigation/fontconfig-tests.log`.

También se conservan dos experimentos negativos: reiniciar datos globales de
Cairo abortó con objetos aún activos; cerrar el display al terminar añadió avisos
de dependencias y no eliminó el fallo global. Ninguna operación se incorporó al
cierre de sesiones. `RUN_DISPLAY_CLOSE_PROBES=1` añade los dos casos opcionales de
cierre del display a la matriz. El experimento de reinicio de Cairo fue retirado
del reproductor tras conservar sus logs `*-dependency-cairo-reset.log`.

La candidata no está instalada ni publicada, y no se ha probado en otras
plataformas. La receta de aplicación y validación está en
[patches/README.md](../patches/README.md). La distribución normal sigue usando
Fontconfig del sistema y conserva sus informes originales.
