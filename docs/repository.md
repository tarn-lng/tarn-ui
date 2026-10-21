# Organización del repositorio

```text
tarn-ui/
  core/                      API semántica y runner de sesión
  backends/linux/gtk/        puente C privado y cabecera ABI
  examples/counter/          programa y lógica de aplicación en Tarn
  devtools/                  lanzador, canal y adaptador MCP en Tarn
  tests/
    gtk/                     ciclo de vida, integración y compile-fail de la API
    compiler/                reproducciones de préstamos y sesión sin GTK
    devtools/                protocolo, permisos y sonda de memoria del canal
    launcher/                aceptación Tarn y referencias del lanzador anterior
  scripts/                   compilación, ejecución y verificación explícitas
  docs/                      guías, resultados y ADR
  evidence/                  informes históricos escritos
  tarn.toml                  identidad y entrada del paquete de fuentes
  .build/                    compilaciones, clones y resultados locales; ignorado
```

Las antiguas carpetas `probes/` y `examples/heading/` se integraron en
`tests/compiler/` y `examples/counter/`. Los scripts activos son
`build-counter.sh`, `run-counter.sh`, `test-gtk.sh` e `investigate-gtk-leaks.sh`.
La guía histórica de la ventana sigue en `docs/native-ui.md`.
El lanzador actual se compila con `build-launcher.sh` y se ejecuta directamente
como `.build/launcher/tarn-ui`, con protocolo interno o `--mode mcp`. La aceptación
está en `test-launcher.sh`; las regresiones nativas siguen en `test-devtools.sh`
y `test-devtools-memory.sh`. Python queda como referencia de pruebas en `tests/`. `devtools.inc` se incluye en el
puente únicamente al compilar con `TARN_UI_DEVTOOLS`.

## Qué llega a GitHub

Se incluyen fuentes, cabeceras, pruebas (incluidos programas que deben fallar al
compilar), scripts, manifiesto y documentación. En `evidence/` se permiten solo
los informes Markdown. Los nombres de archivos locales citados en informes
históricos no implican que esos archivos se publiquen.

`.gitignore` excluye `.build`, `target`, `vendor`, `dist`, bibliotecas y objetos
compilados, registros, capturas/resultados crudos de `evidence`, cachés, archivos
temporales del editor, `.env` y credenciales/configuración local de AWS y agentes.
Se permite expresamente `.env.example` para futuras plantillas sin secretos.
No se borraron los archivos ignorados: siguen disponibles para diagnóstico local.

En la comprobación actual se verificaron 12 rutas que deben ignorarse y 11 que
deben mantenerse visibles, incluyendo fuentes, pruebas, manifiesto, informes y
`.env.example`. No había archivos previamente versionados que hubiera que quitar
del índice; `.gitignore` no elimina del índice archivos que ya estuvieran allí.
No se ejecutó `git add`, `commit` ni `push`.
