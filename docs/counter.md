# Contador nativo: fase 2

> Estado actual: el ejemplo usa [el enlace reactivo](reactivity.md), con ABI 13
> y encabezado par/impar derivado. El flujo imperativo y las versiones de ABI
> descritos abajo son antecedentes de las fases 2–3. `counter.increment` se
> conserva para las fixtures y sus pruebas de compatibilidad.

Actualización de fase 3: la ABI vigente es 3 y el ejemplo confirma cada evento
aplicado con `complete_event`. El canal opcional se documenta en
[la guía de desarrollo](devtools.md); sin `--dev`, el contador no expone transporte.
Las observaciones de ABI 2 y resultados de fase 2 que siguen son históricas.

El ejemplo tiene una ventana, un encabezado, una etiqueta numérica y un botón
GTK4. El valor inicial es cero. El estado `i64` vive en
`examples/counter/main.tarn`; `examples/counter/counter.tarn` implementa el
incremento comprobado. No hay contador de aplicación en C.

## Compilar y ejecutar

```bash
bash scripts/build-counter.sh
bash scripts/run-counter.sh
```

Se requieren Linux x86_64, GTK4/SDK, `pkg-config`, compilador C y el compilador
Tarn fijado ya construido en `.build/verified-da3173c/debug/tarn`. `TARN_BIN`
permite seleccionar explícitamente otro binario. `TARN_SOURCE` selecciona el
checkout cuya integridad se verifica; por defecto es `../tarn`.

En esta sesión, el repositorio vecino recibió cambios externos después de la
verificación inicial. Se conservó intacto y se creó una copia limpia del commit
fijado mediante un clon local en `.build/tarn-source`. Para repetir aquí:

```bash
TARN_SOURCE="$PWD/.build/tarn-source" bash scripts/build-counter.sh
bash scripts/run-counter.sh
```

El script verifica el checkout limpio, compila `libtarn_ui_gtk.so` y prepara en
`.build/counter/source` los módulos de Tarn con la estructura `core/session`.
Después invoca `tarn build --link tarn_ui_gtk`. El manifiesto `tarn.toml` identifica
las fuentes; no instala ni construye dependencias nativas por sí mismo.
Las rutas de enlace y ejecución son absolutas, limitadas al directorio de
compilación. Los diagnósticos JSON, versiones y hashes quedan en `.build/counter`.

## Flujo y contrato

1. Tarn abre una sesión mediante `run` y entrega su valor inicial a `enable_counter`.
2. Los callbacks nativos solo registran eventos. El callback del botón añade
   `Increment` a una cola acotada de 64 eventos idénticos; ese tamaño de cola no
   es el valor del contador.
3. Tarn extrae eventos con `next_event`. Por cada `Increment`, comprueba el límite,
   calcula el siguiente valor y llama a `set_counter`.
4. C formatea exclusivamente el número recibido y cambia la etiqueta GTK. El
   estado de Tarn se confirma después de que la actualización nativa tenga éxito.
5. Cerrar tiene prioridad sobre eventos pendientes. Descarta los clics encolados,
   rechaza nuevas actualizaciones y entrega `Closed` una sola vez.

La suma en `i64::MAX` devuelve el error **9** antes de sumar: no envuelve el entero
ni aborta el proceso. Una cola llena devuelve **8**; una operación después del
cierre devuelve **6**. Los errores de aplicación se propagan sin perder su código.
Un error de presentación no confirma el siguiente estado de Tarn.

El runner conserva el resultado del cuerpo, libera la sesión y después devuelve
ese resultado. Tanto `try` dentro del cuerpo como el retorno temprano siguen ese
camino. Los abortos, la terminación forzada y `os.exit` no desenrollan recursos.

El ingreso sigue marcado `unsafe` con precondición de hilo principal y ausencia
de sesiones anidadas. C verifica el hilo principal de Linux antes de acceder a
GTK; el `Session` opaco conserva su puntero privado y no puede transferirse a una
tarea mediante Tarn seguro. La ABI se elevó a **2**; el wrapper rechaza una versión
distinta antes de crear widgets. No se modificó el compilador ni su confianza.

La ventana conserva una referencia explícita adicional a la referencia interna
de GTK. Al habilitar el contador, una referencia temporal mantiene vivo el
encabezado mientras se cambia de padre; luego el contenedor es su único dueño.
Los hijos quedan a cargo del árbol GTK. La liberación invalida la sesión,
descarta eventos, desconecta las tres señales, destruye la ventana y libera
su referencia y almacenamiento. No se instalan temporizadores de producción.

## Verificación puntual

```bash
bash scripts/test-gtk.sh --build-only
bash scripts/test-gtk.sh
bash scripts/investigate-gtk-leaks.sh
```

La primera orden solo compila. La segunda abre ventanas breves y conserva el
resultado de cada prueba, aunque otra falle. La tercera ejecuta una investigación
separada de LSan con GTK sin el puente. Los resultados locales quedan en
`.build/evidence/phase2` y `.build/evidence/leak-investigation`.

No se ejecuta `cargo test` ni la suite completa en estas órdenes. Actualmente las
órdenes con LSan terminan con **1**, no con éxito global: consulta
[los resultados medidos](phase2-results.md). No hay supresiones ni desactivación
de detección de fugas. La captura de las pruebas usa el widget GTK renderizado
después de un frame; no es un canal de captura de la aplicación ni una API nueva.
