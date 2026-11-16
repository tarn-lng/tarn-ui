# Demo `examples/cairn_b2_welcome` (Milestone B2)

Demostración del hito **B2** del backend nativo `cairn` de `tarn_ui` en Tarn puro:
- Motor tipográfico TrueType/OpenType en Tarn puro (`text.tarn`): carga `Bricolage Grotesque` y `Onest` (licencia OFL en `backends/cairn/fonts/`), decodificación UTF-8 con acentos españoles (`á é í ó ú ñ ü ¡ ¿`), kerning `GPOS`, numerales tabulares (`tnum`), posicionamiento subpíxel horizontal (4 fases), alineación por línea base, truncado con elipsis (`…`) y composición de párrafos UAX #14 con `text-wrap: balance`.
- Iconos vectoriales Lucide (`icons.tarn`, licencia ISC en `backends/cairn/icons/Lucide-ISC.txt`) y marcas de identidad Cairn (`CairnMark`, `CairnIcon`, `CairnLogo`, `AppIcon`).
- Escena de bienvenida del onboarding (`SetupWindow` + `Sidebar` + `StepWelcome`) sobre `Wallpaper("lago")` y ventana `.glass.window` con seguimiento de daño por tiles, más vista interactiva `BrandSheet` (`Identidad de marca`).

## Comandos

```sh
nice -n 10 bash scripts/build-cairn-b2.sh
nice -n 10 bash scripts/run-cairn-b2.sh
nice -n 10 bash scripts/run-cairn-b2.sh --brand
nice -n 10 bash scripts/run-cairn-b2.sh --headless /tmp/cairn-b2-welcome.png
nice -n 10 bash scripts/run-cairn-b2.sh --bench
```
