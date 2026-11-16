# Backend Cairn (`backends/cairn`)

El tercer backend de `tarn_ui` (`ABI 14`), escrito íntegramente en Tarn puro:
dibuja cada píxel, gradiente, borde con distancia firmada (SDF), reflejo
especular y sombra 9-slice sin delegar en controles de GTK ni AppKit.

```sh
nice -n 10 bash scripts/test-cairn.sh          # pruebas unitarias, snapshot PNG determinista y ventana host
nice -n 10 bash scripts/build-cairn-b1.sh      # compila examples/cairn_b1_rects
nice -n 10 bash scripts/run-cairn-b1.sh        # abre la demo interactiva B1
nice -n 10 bash scripts/run-cairn-b1.sh --headless /tmp/cairn-b1.png
```

## Módulos (`backends/cairn/backend/`)

- `damage.tarn` — `Rect` y `DamageRegion` acotada (hasta 16 rectángulos AABB antes de fusionar, alineación por tiles de 32×32 px). Cuando está vacía, el bucle de renderizado hace cero trabajo (`0%` CPU en reposo).
- `render.tarn` — `Canvas` BGRA8 premultiplicado, mezcla de color (`color-mix` sRGB), rectángulos redondeados analíticos anti-alias con radios por esquina, bordes internos/externos de 1 px (`--edge`, `--line`), reflejo superior interno de 1 px (`--hi`), gradientes lineales y radiales, sombras 9-slice precalculadas y generador procedural del paisaje `Wallpaper("lago")`.
- `png.tarn` — codificador PNG en Tarn puro (zlib sin compresión + Adler-32 + CRC-32 + SHA-256) para `snapshot_png` sin requerir servidor gráfico ni permisos de captura.
- `objc.tarn` / `surface_macos.tarn` — ventana host en macOS (`NSWindow` + `CALayer` + `CGDataProvider`/`CGImage` con `contentsScale` Retina y seguimiento de puntero/clics/teclado).
- `surface_linux.tarn` — stub documentado para la superficie Linux Wayland (`wl_shm`), previsto para pruebas en entorno Linux.
- `native.tarn` — contrato `native.Native` (`ABI 14`) y escena B1 retenida con seguimiento de daño por tiles.
