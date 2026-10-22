# Estado y próximos pasos

Actualizado el 22 de octubre de 2026.

## Fase 5: reactividad y componentes nativos

El usuario autorizó continuar con reactividad y el resto de la interfaz,
aplazando la investigación de memoria para evitar que bloquee el producto.

- Implementado: estado propio con igualdad, reducer, vista derivada y enlace
  reactivo del contador que conserva los controles GTK.
- Implementado: etiquetas, botones y páginas con claves estables, vistas con
  diferencias de propiedades y navegación entre dos pantallas con estado
  compartido, pila de rutas y acción de volver.
- Siguiente: disposición nativa más general, foco, accesibilidad y tema sobre
  esos componentes, con pruebas de identidad y disposición de cada pantalla.

Las entregas están documentadas en [reactivity.md](reactivity.md) y
[componentes y navegación](components-navigation.md). La composición actual
usa páginas de topología fija; la reconciliación de árboles queda pendiente.

## Recordatorio aplazado: Mesa/EGL

Retomar cuando el usuario lo solicite o al planificar el endurecimiento de una
versión distribuible. No bloquea el trabajo actual de reactividad/componentes.

Con Fontconfig candidato local quedan **56736 bytes en 276 asignaciones de
Mesa/EGL** en el renderizador predeterminado, iguales con GTK solo y con el
puente. El baseline del sistema sigue fallando; no se añadieron supresiones.
La candidata de Fontconfig no está instalada en el sistema.

Punto de reanudación: [investigación y receta reproducible](memory-status.md),
incluyendo el parche experimental y los registros locales. No repetir ahora
la investigación de dependencias ni la suite completa de Cargo.
