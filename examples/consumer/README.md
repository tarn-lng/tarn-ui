# Consumidor de tarn_ui

Proyecto independiente que usa `tarn_ui` como dependencia de registro, sin
copiar fuentes. Publica el paquete y compila:

```bash
bash ../../scripts/package.sh            # registro local en ui/.build/registry
tarn fetch && tarn build main.tarn -o contador
```

El `tarn.lock` fija el registro local de esta máquina; en otro equipo vuelve a
ejecutar `tarn add tarn_ui --version '^0.1.0' --registry file:///.../registry/`.
