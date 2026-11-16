# Demo Milestone B1: `examples/cairn_b1_rects`

Renderiza en Tarn puro (`backends/cairn`) el fondo procedural `Wallpaper("lago")`,
la ventana `.glass.window` con esquinas redondeadas analíticas (SDF), borde
interno de 1 px (`--edge`), reflejo especular superior de 1 px (`--hi`),
sombras 9-slice precalculadas y tarjetas interactivas (`.card` / `.card.is-on`)
con estados de hover y clic activo.

Cuando el puntero se detiene, `DamageRegion` queda vacía y el bucle principal
bloquea en `nextEventMatchingMask:` con **0 redibujos y ~0 % de CPU**.

## Compilación y ejecución

```sh
nice -n 10 bash scripts/build-cairn-b1.sh
nice -n 10 bash scripts/run-cairn-b1.sh
```

Modos sin ventana interactiva (para verificación determinista y mediciones):

```sh
nice -n 10 bash scripts/run-cairn-b1.sh --headless /tmp/cairn-b1.png
nice -n 10 bash scripts/run-cairn-b1.sh --bench
```
