# Canal de desarrollo GTK4

La variante de desarrollo permite inspeccionar el árbol GTK real, activar el botón
y cerrar la ventana. El estado del contador y el incremento siguen en Tarn.
El binario normal no contiene el transporte ni depende de JSON-GLib.

## Compilar y abrir una sesión autorizada

```bash
TARN_SOURCE="$PWD/.build/tarn-source" bash scripts/build-counter.sh --dev
bash scripts/build-launcher.sh
.build/launcher/tarn-ui --allow inspect click close --allow-text
```

`TARN_SOURCE` selecciona el checkout limpio del commit fijado; puede omitirse si
`../tarn` cumple esa condición. El compilador ya construido se selecciona con
`TARN_BIN` o, por defecto, `.build/verified-da3173c/debug/tarn`. No se recompila ni
modifica Tarn. Además de GTK4, la variante de desarrollo requiere JSON-GLib
**1.10 o posterior** para el analizador JSON estricto. El lanzador usa Tarn y las
APIs públicas de su biblioteca estándar. Su script de compilación usa `../tarn/target/debug/tarn` por defecto y requiere las
correcciones hasta `39c4617` (ADR 0074); el ejemplo GTK conserva su pin independiente.
Se requiere Linux 5.3 o posterior para la espera con pidfd. El [informe de
migración](launcher-migration.md) detalla plazos, errores y resultados.

El lanzador crea un `socketpair` Unix privado y entrega solo un extremo al hijo.
No hay un puerto, listener, ruta pública de socket ni credencial en argumentos.
La posesión del descriptor heredado es la capacidad del canal. Al iniciarlo se
comprueban el tipo de socket y las credenciales de usuario del par.

Los permisos de lanzamiento se fijan mediante `--allow`; no pueden ampliarse por
una solicitud. `--allow-text` permite leer títulos y etiquetas nativas; sin esa
opción, `text` es `null` y `redacted` es `true`. El permiso `inspect` permite leer
la estructura y metadatos. No se registran snapshots automáticamente.

La aplicación genera un `session_id` nuevo y anuncia `ready` cuando los widgets
visibles relevantes tienen asignación real de tamaño. El lanzador confirma ese
identificador mediante `session.authorize` antes de enviar operaciones. Esta
confirmación vincula el canal a la instancia; el identificador no es un secreto
ni reemplaza la capacidad del descriptor.

Un binario de desarrollo sin descriptor heredado no instala el canal. La
variante normal, creada sin `--dev`, omite completamente su código aunque el
entorno contenga variables de desarrollo.

## Solicitudes y respuestas

El lanzador muestra `ready` y acepta un objeto JSON por línea en su entrada:

```json
{"version":1,"request_id":"inspect1","method":"ui.inspect","params":{"window_id":"main"}}
{"version":1,"request_id":"click1","method":"ui.click","params":{"window_id":"main","element_id":"increment"}}
{"version":1,"request_id":"close1","method":"ui.close","params":{"window_id":"main"}}
```

La respuesta conserva `version` y `request_id`, y contiene `result` o
`error:{code,message}`. Los IDs deben ser únicos durante el canal, de 1 a 128
caracteres ASCII alfanuméricos, `_` o `-`. Las solicitudes inválidas cuyo ID no
pueda recuperarse y los fallos terminales del canal usan `request_id:null`.
El bootstrap reservado `session.authorize` acepta únicamente `session_id`.

No se admiten campos adicionales, claves duplicadas, tipos incorrectos, UTF-8
inválido, NUL, comentarios JSON ni versiones distintas de 1. La captura se autoriza con `--allow screenshot --allow-capture`; recibe
`window_id` y opcionalmente `min_revision`. Espera un frame real y devuelve PNG,
dimensiones, revisión y hash. No existen métodos de escritura de texto, shell
o reconstrucción. `--mode mcp` usa el adaptador MCP stdio en Tarn; consulta el
[contrato y sus pruebas](launcher-migration.md#mcp-stdio-boundary).

### `ui.inspect`

Recorre los hijos GTK en su orden nativo, en el hilo de UI. Los IDs de aplicación
son `main`, `heading`, `content`, `counter` e `increment`. Los hijos internos
reciben rutas deterministas, como `increment/0`, marcadas `stable_id:false`.
Los IDs duplicados de aplicación impiden iniciar la sesión de desarrollo.

Cada nodo incluye su clase GTK real, rol accesible nativo, texto obtenido mediante
getters cuando está autorizado, sensibilidad efectiva, visibilidad, padre e hijos.
No hay una copia del contador ni una jerarquía declarada usada como sustituto.
La etiqueta accesible independiente se declara no disponible: no se inventa a
partir del texto. Las coordenadas son medidas en el espacio lógico de la ventana,
con escala; las coordenadas globales son `null` con motivo explícito. Un widget
oculto o sin tamaño asignado no recibe límites ficticios.

El resultado incluye `session_id`, `revision`, plataforma, backend y versión real
de GTK. El recorrido se limita a 32 niveles y declara `children_truncated` si llega
a ese límite; el árbol de este ejemplo es mucho menor.

### `ui.click`

Resuelve el ID sobre los widgets actuales, verifica que sea un botón visible y
habilitado e invoca su señal nativa `clicked` de forma síncrona. El callback solo
encola `Increment` con un token de correlación; no llama a Tarn ni incrementa el
valor. Los clics físicos usan el mismo callback y no llevan token remoto.

Tarn extrae el evento, ejecuta el incremento comprobado y actualiza la etiqueta.
Después de confirmar su estado local, llama a `Session.complete_event()`. Solo
entonces se responde al clic con la revisión aplicada. Las solicitudes posteriores
esperan esa respuesta, de modo que un `inspect` encolado observa la actualización.
Cada evento completado incrementa la revisión; las inspecciones no la cambian.

Si el cuerpo termina con error antes de completar, la solicitud pendiente recibe
`NATIVE_ERROR`, no éxito. Un cierre nativo durante la acción produce
`WINDOW_CLOSED`. La falta de confirmación produce `TIMEOUT` a los dos segundos,
según reloj monotónico; se comprueba también al confirmar, aunque un manejador
lento haya impedido ejecutar el temporizador. En ese caso se cancela el canal.
Un timeout o una desconexión no revierte acciones ya admitidas: no debe asumirse
que es seguro repetirlas como si no hubieran ocurrido.

### `ui.close`

Encola el cierre nativo y deja que Tarn devuelva el control al runner. Este
desconecta señales, destruye los widgets y libera sus referencias. Solo después
se responde con `closed:true` y una revisión nueva. Las solicitudes completas ya
encoladas detrás del cierre reciben `WINDOW_CLOSED`. El descriptor y todas sus
fuentes GLib se eliminan al terminar la sesión.

## Límites y cancelación

El canal tiene un presupuesto de entrada acumulada de 1 MiB, incluidas las
terminaciones de línea, hasta 32 registros pendientes, JSON de hasta 64 niveles,
búfer de salida limitado a 2 MiB e historial de hasta 1024 IDs. La cola nativa
de incrementos sigue limitada a 64. Un exceso terminal de entrada cancela el
canal con `QUEUE_FULL` o `INVALID_REQUEST`; no ejecuta la cola descartada.

La E/S es no bloqueante. Un lector que no drena respuestas puede perderlas al
destruir la sesión; no se fabrica una confirmación de entrega o de éxito. El
cliente tiene que tratar EOF o vencimiento como cancelación. El ejemplo sigue
siendo una aplicación normal si se pierde el canal.

El lanzador intenta `ui.close` al salir. Si no tiene ese permiso o el canal ya
falló, hace una cancelación acotada del proceso y sale con 2; declara explícitamente
que esa terminación no es un cierre nativo limpio. Las pruebas también distinguen
esos escenarios negativos de la aceptación normal.

## Verificar

```bash
bash scripts/test-devtools.sh --build-only
bash scripts/test-devtools.sh
bash scripts/test-devtools-memory.sh
```

Son pruebas puntuales; no ejecutan la suite completa. Los resultados están en
[el informe de fase 3](phase3-results.md). La sonda de memoria conserva el código
de salida nativo y no desactiva LeakSanitizer. La regresión de renderizado anterior
sigue reproduciendo sus fugas y se informa por separado.
