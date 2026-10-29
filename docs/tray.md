# Bandeja, notificaciones y segundo plano

ABI **14** permite que una app de Tarn UI viva en la bandeja del sistema. La
integración con el escritorio está escrita en Tarn sobre
[tarn-dbus](../../libs/dbus): no hay código C nuevo para D-Bus. El puente GTK
solo añade el modo segundo plano y un aviso para despertar el bucle de eventos.

```bash
bash scripts/build-tray.sh
LD_LIBRARY_PATH=.build/tray .build/tray/tray
bash scripts/test-tray.sh
```

El ejemplo [`examples/tray`](../examples/tray/main.tarn) muestra una ventana
que, al cerrarse, sigue en la bandeja. El icono la muestra u oculta. El menú
ofrece Mostrar, una casilla «No molestar» y Salir. «Avisar» envía una
notificación cuya acción «Abrir» trae la ventana de vuelta.

## Cómo funciona

- `desktop.connect(id, vista)` abre el bus de sesión, publica el
  StatusNotifierItem con su menú y se suscribe a las notificaciones.
- `ui.watch_fd(desk.fd())` vigila el socket del bus desde GLib con un
  `ffi.callback` estático. Cuando hay datos, la sesión entrega
  `Action.Wake`; varias señales seguidas se agrupan en una. No hay hilos ni
  sondeo: en reposo la app no consume CPU.
- `desk.poll()` no bloquea. Responde las llamadas del anfitrión de la bandeja
  y devuelve `Signal`: `Activate`, `Menu(id)`, `Notification`, `TrayShown` y
  `TrayGone`. Llámalo tras `Wake` y tras cualquier otra acción, porque una
  llamada bloqueante como `notify` puede dejar mensajes en cola sin despertar
  la sesión.
- `desk.apply(vista)` publica la `TrayView` derivada del modelo. Solo se envía
  lo que cambió, igual que con `View`.

## Segundo plano

`ui.set_background(true)` hace que cerrar la ventana la oculte y entregue
`Action.Hidden` en lugar de `Closed`. `ui.present()`, `ui.hide()` e
`ui.is_visible()` controlan la ventana. `ui.request_close()` termina la sesión
también en segundo plano.

Activa el modo solo si `desk.has_tray()`: sin anfitrión de bandeja la ventana
oculta quedaría inalcanzable. El ejemplo lo desactiva al recibir `TrayGone` y
lo reactiva con `TrayShown`. Si no hay bandeja, cerrar la ventana termina la
app como siempre.

## Pruebas

`scripts/test-tray.sh` corre en un bus de sesión aislado sin activación de
servicios ([`tests/tray/session.conf`](../tests/tray/session.conf)). Los
fixtures de tarn-dbus hacen de anfitrión de bandeja y de servidor de
notificaciones. La prueba cubre ocultar al cerrar, el aviso único, el icono,
el menú, la acción de la notificación, la casilla, la CPU en reposo, la salida
con liberación del nombre y el caso sin bandeja.

El bus aislado no debe activar servicios. Si GTK arranca allí
`at-spi-bus-launcher`, este escribe su socket en el
`$XDG_RUNTIME_DIR/at-spi/bus_0` real y deja sin accesibilidad al escritorio.
