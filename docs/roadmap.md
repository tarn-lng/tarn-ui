# Estado y próximos pasos

Actualizado el 28 de octubre de 2026.

## Fase 5: reactividad y componentes nativos

El usuario autorizó continuar con reactividad y el resto de la interfaz,
aplazando la investigación de memoria para evitar que bloquee el producto.

- Implementado: estado propio con igualdad, reducer, vista derivada y enlace
  reactivo del contador que conserva los controles GTK.
- Implementado: etiquetas, botones y páginas con claves estables, vistas con
  diferencias de propiedades y navegación entre dos pantallas con estado
  compartido, pila de rutas y acción de volver.
- Implementado: filas y columnas anidadas, foco por página, encabezados y
  nombres accesibles, estilos semánticos del tema nativo y pruebas de disposición.
- Implementado: entrada de texto con snapshots ordenados, estado Tarn y textos
  derivados; nombre editable del contador con indicación cuando está vacío.
- Implementado: envío local con borrador y nombre confirmado, validación de
  nombres vacíos y controles habilitados derivados mediante diff de propiedades.
- Implementado: restablecer al último nombre confirmado y sincronizar entradas
  desde el modelo sin generar nuevos eventos de edición.
- Implementado: `ui.fill` con permiso explícito por protocolo y MCP; pruebas del
  formulario real de extremo a extremo sin hooks de edición.
- Implementado: primera reconciliación de listas con claves estables, edición,
  reordenamiento y eliminación de filas nativas, con pruebas de identidad y limpieza.
- Implementado: regiones mixtas con grupos anidados, ramas condicionales y campos
  accesibles; conservación de identidad, desmontaje recursivo y recuperación del
  estado desde Tarn, con pruebas de límites y limpieza.
- Implementado: páginas con desplazamiento vertical nativo y altura acotada por
  el monitor, ajuste de texto en cualquier carácter, captura con viewport
  explícito y auditoría aislada del CSS de usuario.
- Implementado: pruebas nativas de Tab/Shift+Tab, activación por teclado,
  retiro de la rama enfocada y propiedades accesibles reales de campos dinámicos.
- Revisión de cierre repetida el 28 de octubre: [matriz y mediciones](phase5-review.md).
  Los criterios se cumplen en el alcance medido; queda recomendada una
  comprobación manual con lector de pantalla antes de presentar la preview.
- La plantilla de proyecto es una propuesta para la preview, separada del siguiente
  bloque técnico del plan original. La fase 6 continúa siendo preparación móvil.

Las entregas están documentadas en [reactivity.md](reactivity.md) y
[componentes y navegación](components-navigation.md). La composición actual
usa páginas de topología fija con [regiones de listas dinámicas](dynamic-lists.md);
las [regiones mixtas](dynamic-regions.md) añaden subárboles condicionales. Los
cambios de tipo o padre por clave y la virtualización siguen fuera de alcance.

## Widgets nativos y escritorio

- Implementado (ABI 13): [controles de formulario](widgets.md) en páginas estáticas.
- Implementado: indicadores de progreso, nivel y actividad.
- Implementado: contenedores (marco, desplegable y pestañas).
- Implementado: texto multilínea, barra de título y menú principal.
- Implementado: controles, indicadores, marcos, desplegables y texto multilínea
  dentro de regiones dinámicas (las pestañas siguen siendo estáticas).
- Implementado (ABI 14): [bandeja, notificaciones y segundo plano](tray.md) en
  Tarn puro sobre tarn-dbus, probado en un bus aislado y a mano en Plasma.

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
