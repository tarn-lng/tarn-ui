# Backend AppKit (macOS)

El mismo contrato `native.Native` (ABI 14) que el backend GTK, escrito en Tarn
sobre el runtime de Objective-C: sin C, sin Objective-C y sin `-framework`.
AppKit se carga con `dlopen`; solo se enlazan `objc` y `CoreFoundation`.

```sh
bash scripts/test-appkit.sh          # pruebas del backend (sesión gráfica)
```

Los scripts de compilación eligen este backend en macOS (`scripts/backend.sh`):
lo copian como `backend/*.tarn` junto a `core/session.tarn`.

## Cómo llama a AppKit

`backend/objc.tarn` declara una sola vez `objc_msgSend` con 6+4 argumentos
enteros/punteros y 4 `f64`. Como `objc_msgSend` no es variádica, enteros y
dobles ocupan sus registros en orden (x86_64 System V y arm64) y el método
ignora lo que sobra. El diseño usa `NSStackView` y anclas de Auto Layout; el
único `NSRect` (capturas) va a la vez en 4 registros flotantes (arm64) y en 4
huecos de pila (x86_64), y cada ABI ignora la otra copia.

Los eventos llegan por una clase `TarnUITarget` creada en tiempo de ejecución
(`ffi.callback` como IMP) a una cola en memoria propia, como en GTK. Los
temporizadores (`NSTimer`) y descriptores (`CFFileDescriptor`) se registran en
`kCFRunLoopCommonModes` y despiertan `nextEvent` con un evento de aplicación.

## Estado

Funciona: ciclo de vida, contador, modo en segundo plano, páginas, cajas,
textos, botones, campos de texto (cambio y envío), primario, habilitado,
accesibilidad, foco, listas y árboles dinámicos (misma reconciliación por
clave que GTK), `watch_fd`, `watch_interval` y `snapshot_png` (captura de la
ventana sin permiso de grabación de pantalla, en modo claro u oscuro).
`examples/navigation` funciona salvo sus opciones plegables.

Aún no (estado 11, "not supported by this platform backend yet"): controles
de formulario, contenedores, áreas de texto, barra de cabecera y menú,
`set_escape_action`. `set_class`, `set_follow_end` y
`scroll_to_end` se aceptan sin efecto (no hay CSS ni páginas con scroll aún).
Sin canal de devtools.
