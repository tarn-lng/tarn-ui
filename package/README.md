# tarn_ui

Interfaz nativa de escritorio para Tarn sobre GTK4 (ABI 14).

```toml
[dependencies]
tarn_ui = { version = "^0.1.0", registry = "file:///ruta/al/registro/" }

# Solo el manifiesto de la aplicación concede bibliotecas nativas (ADR 0091).
[native]
links = ["gtk-4", "gobject-2.0", "gio-2.0", "glib-2.0"]
```

```tarn
import "tarn_ui/core/session"
import "tarn_ui/core/reactive"
```

Módulos: `core/session`, `core/reactive`, `core/components`,
`core/navigation`, `core/forms`, `core/desktop` (bandeja, menú y
notificaciones; incluye tarn-dbus en `src/` y `desktop/`). El paquete se publica sin el canal de
devtools; las compilaciones `--dev` siguen usando los scripts del repositorio.


## Observación de ventanas (0.4.2)

`core/accessibility.observe(pid, title)` recoge un árbol AT-SPI acotado de una
ventana inequívoca; omite contraseñas y conserva roles, texto, rectángulos y
estados. No activa ventanas ni cambia preferencias. Requiere `--link atspi`.

`core/window_capture.capture(handle)` captura una ventana KWin ScreenShot2 sin
foco, sombra ni decoración. `Frame.save(path, elements, marked)` guarda PNG
con o sin marcas. Requiere `--link gio-2.0 --link gobject-2.0 --link glib-2.0
--link cairo`. Las coordenadas AT-SPI son lógicas y relativas a la ventana;
la imagen usa su escala declarada. Los números solo identifican esa observación.
El consumidor decide permisos, almacenamiento y cómo tratar datos externos.

El contenedor de páginas mantiene sus márgenes en una caja interior para que
GtkStackPage no los sume dos veces al informar coordenadas por AT-SPI.


## Acciones AT-SPI (0.5.0)

Cada `Element` incluye `route`, `reference` y `actions`. La referencia usa el
proveedor D-Bus y su ruta de objeto; las rutas se leen de los campos públicos
de AtspiObject y AtspiApplication, consultando a GType el tamaño de su padre
GObject (ABI Linux de 64 bits).

`accessibility.perform(pid, title, expected, operation, value)` vuelve a resolver
y validar la ventana y el elemento antes de actuar. `write` reemplaza el texto
y verifica su lectura; `invoke` busca una acción por nombre exacto. Una acción
aceptada no garantiza su efecto semántico: el consumidor debe observar de nuevo.
El consumidor aplica la autorización y evita repeticiones tras un timeout.

## Entrada por portal

`core/remote_desktop` ofrece una sesión de teclado/puntero sin capturas ni
persistencia. Llamar `poll` al despertar `fd` y periódicamente (por ejemplo con
`Session.watch_interval(100)`). `ready` indica que terminó el permiso del
escritorio. `motion`, `click` y `keysym` usan únicamente los métodos D-Bus
Notify; esta API no conecta EIS. El propietario debe llamar `close` incluso al
cancelar o salir. Los pares de presión/liberación intentan soltar tras un
error y cierran la sesión si el resultado es incierto. La aplicación decide
el destino y comprueba el efecto: el portal no garantiza foco ni éxito de UI.

## Contadores de actividad (0.7.0)

`core/activity` proporciona `Monitor` para evdev Linux x86_64 y `Client` para
recibir únicamente un contador y un indicador de pulsación mantenida por una
FIFO. No conserva códigos de tecla, texto ni coordenadas. El emisor abre los
dispositivos con la autorización necesaria y puede abandonar privilegios antes
de llamar `publish`. No captura dispositivos en exclusiva. EOF, pérdida de
eventos, cambios de dispositivos y latidos ausentes se informan como errores;
el consumidor decide cómo pausar su trabajo. Debe cerrar monitor y cliente.
