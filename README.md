# Tarn UI

Reutilizable como paquete `tarn_ui`: `bash scripts/package.sh` lo publica en un
registro local y [examples/consumer](examples/consumer/README.md) lo usa como
dependencia con `tarn add` y `tarn build` ([detalles](package/README.md)).

La fase 5 tiene [estado reactivo y vistas derivadas](docs/reactivity.md) y
[componentes reutilizables con navegación](docs/components-navigation.md).
Las pantallas comparten estado y conservan sus controles GTK al volver.
La demo incluye una [lista dinámica editable](docs/dynamic-lists.md) que conserva
la identidad de las filas al reordenarlas y libera las retiradas.
Las [opciones condicionales](docs/dynamic-regions.md) montan y retiran un bloque
con campo, encabezado y botones conservando su estado en Tarn.
El formulario permite guardar y restablecer un nombre; el agente puede probarlo
mediante [`ui.fill` con permiso explícito](docs/devtools.md#uifill).
[Controles nativos de formulario](docs/widgets.md): interruptor, casilla,
deslizador, campo numérico y desplegable.
[Bandeja, notificaciones y segundo plano](docs/tray.md) en Tarn puro sobre tarn-dbus (ABI 14).
Los [errores](docs/errors.md) dicen qué operación falló, con qué clave y por qué.
[Próximos pasos y recordatorio de memoria aplazado](docs/roadmap.md).
Las páginas se desplazan verticalmente y el texto largo se ajusta al ancho; la
[revisión de fase 5](docs/phase5-review.md) mide disposición, teclado, temas y contraste.

Contador nativo GTK4 con estado y lógica de aplicación en Tarn. El puente C
encola eventos y presenta los valores que recibe; Tarn procesa los clics.

```bash
bash scripts/build-counter.sh
bash scripts/run-counter.sh
```

Consulta [la guía del contador](docs/counter.md), [los resultados reales de fase 2](docs/phase2-results.md)
y [la estructura y política de archivos](docs/repository.md).

La fase 3 añade un [canal de desarrollo autorizado](docs/devtools.md) para
inspección, clic y cierre. Se compila aparte con `--dev`; el binario normal no
contiene ese transporte. [Resultados de fase 3](docs/phase3-results.md).

Las pruebas puntuales de interacción, límites, errores y préstamos pasan.
El lanzador se compila con `bash scripts/build-launcher.sh` y se ejecuta con
`.build/launcher/tarn-ui --allow inspect click close --allow-text`.
LeakSanitizer sigue fallando también con GTK sin el puente; el fallo se conserva.
[Investigación actual de memoria y reproductor mínimo](docs/memory-status.md).
El lanzador y el adaptador MCP stdio ya usan Tarn y un socketpair privado;
[compilación, permisos y resultados de migración](docs/launcher-migration.md).
La fase 4 añade compilación fija por MCP y capturas PNG validadas en Tarn,
con guardado privado opcional (directorio 0700 y archivos 0600).
La captura requiere autorización explícita. Hay estado reactivo, componentes
con claves y navegación básica. Los árboles dinámicos y backends móviles siguen pendientes.

[ADR 0001](docs/adr/0001-native-ui-foundations.md) conserva la arquitectura y los
contratos de seguridad. `evidence/` contiene informes históricos; sus registros
y capturas locales están excluidos de Git. El manifiesto de fuentes usa
`tarn_ui`; el puente nativo se compila mediante el script explícito del proyecto.


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
