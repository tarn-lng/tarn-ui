# Demo `examples/cairn_b3_motion` (Milestone B3)

Demostración del hito **B3** del backend nativo `cairn` de `tarn_ui` en Tarn puro:
- Resortes analíticos de segundo orden interrumpibles (`anim.tarn`): `step-pill` (`420/36`), `Segmented` (`500/36`), `Toggle` (`600/34`), botones hover/press (`500/32`) y `CairnMark` (`220/18`).
- Transición de pasos de onboarding (`Bienvenida` <-> `Idioma y región`) con curva `cubic-bezier(0.22, 0.8, 0.3, 1)` (`0.22s`).
- Desenfoque de fondo Dual-Kawase a resolución reducida con saturación (`blur(38px) saturate(1.9)`) en caché por superficie (`render.tarn`).
- Sincronización con la tasa de refresco de la pantalla (`NSScreen.maximumFramesPerSecond`) y `0` redibujos en reposo.

## Comandos

```sh
nice -n 10 bash scripts/build-cairn-b3.sh
nice -n 10 bash scripts/run-cairn-b3.sh
nice -n 10 bash scripts/run-cairn-b3.sh --headless /tmp/cairn-b3-motion
nice -n 10 bash scripts/run-cairn-b3.sh --bench
```
