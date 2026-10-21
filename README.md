# Tarn UI

Contador nativo GTK4 con estado y lógica de aplicación en Tarn. El puente C
encola eventos y presenta los valores que recibe; Tarn procesa los clics.

```bash
bash scripts/build-counter.sh
bash scripts/run-counter.sh
```

Consulta [la guía del contador](docs/counter.md), [los resultados reales de fase 2](docs/phase2-results.md)
y [la estructura y política de archivos](docs/repository.md).

Las pruebas puntuales de interacción, límites, errores y préstamos pasan.
LeakSanitizer sigue fallando también con GTK sin el puente; el fallo se conserva.
No se repitió la suite completa. No hay MCP, componentes reactivos, navegación
ni backends móviles.

[ADR 0001](docs/adr/0001-native-ui-foundations.md) conserva la arquitectura y los
contratos de seguridad. `evidence/` contiene informes históricos; sus registros
y capturas locales están excluidos de Git. El manifiesto de fuentes usa
`tarn_ui`; el puente nativo se compila mediante el script explícito del proyecto.
