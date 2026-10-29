# Errores de Tarn UI

Cada operación que falla devuelve un `session.Error` con dos datos:

- `code`, el estado del bridge (`backends/linux/gtk/bridge.h`): 1 entrada
  inválida, 2 hilo equivocado, 3 sesión activa, 4 sin pantalla, 5 fallo de
  creación, 6 ventana cerrada, 7 ABI distinta, 8 cola llena.
- `context`, qué operación falló y con qué clave, como `set_text "summary"`,
  o la causa concreta en las comprobaciones hechas en Tarn: rutas de
  navegación, claves repetidas en vistas y regiones, anidación demasiado
  profunda.

`error.message()` junta el contexto y la razón del código:

```text
set_text "missing": invalid input (unknown key, duplicate key, bad parent or value out of range)
navigation: unknown route "ajustes"
region: duplicate key "nota"
```

`error.reason()` da solo la razón. Las aplicaciones crean sus propios errores
con `session.fail(code, context)`. Los ejemplos imprimen `error.message()` al
terminar con error. Comparar `code` sigue funcionando igual que antes.
