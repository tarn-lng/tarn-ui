# Primera entrega de reactividad

El contador usa estado propio en Tarn, un reducer y una vista derivada. Al pulsar
Incrementar cambian el número y el encabezado «par/impar». La aplicación ya no
coordina manualmente los setters y las confirmaciones de cada evento.

```tarn
var state: reactive.State<i64> = reactive.State.new(0)
// Dentro de session.run:
return reactive.drive_counter(ui, &mut state,
    fn(value i64, event session.Event) Result<i64, session.Error> {
        return counter.reduce(value, event)
    },
    fn(value i64) reactive.CounterView { return counter.view(value) })
```

`core/reactive.tarn` contiene `State<T: Eq>`, con valor privado, lectura prestada,
`set` y señal de cambios pendientes. Escribir un valor igual devuelve false y
no añade trabajo. Varias escrituras antes de entrar al enlace se presentan como
un único estado inicial. No hay observadores globales ni callbacks almacenados
en el estado.

El primer enlace es deliberadamente concreto: `drive_counter` recibe
`State<i64>`. El reducer recibe número/evento y devuelve el siguiente número o un
error; la vista recibe el número y devuelve `CounterView`, con número y texto
propios. Los callbacks no reciben referencias a widgets ni a la sesión nativa.
Esto no implementa todavía un grafo de señales, efectos asíncronos, componentes
arbitrarios ni reconciliación de árboles. La siguiente entrega añade
[componentes con claves y navegación](components-navigation.md).

El enlace monta los controles una sola vez, vuelve a evaluar la vista si cambió
el estado y compara cada propiedad con la descripción anterior. Una proyección
que produce la misma vista no escribe en GTK. Una acción sin cambios se confirma
igualmente, después de comprobar que no necesita presentación. Un cierre sale
sin invocar reducer/vista. Todo sucede en el hilo UI y dentro del runner scoped.

El modelo se actualiza antes de aplicar la vista. Si falla un setter, permanece
pendiente y el error vuelve al runner para cerrar la sesión; no hay rollback ni
atomicidad entre varios setters. El clic nunca se confirma como exitoso en ese
caso. El límite i64 conserva el error 9 sin abortar ni desbordar.

Esta entrega introdujo ABI 5 con `Session.set_heading`; la ABI vigente es **11**
tras incorporar componentes y páginas. El puente valida hilo,
cierre, cola y UTF-8, y conserva la etiqueta original al actualizar su texto.
Hay que recompilar biblioteca y aplicación juntas. El número de versión del
protocolo de desarrollo no cambia.

## Ejecutar y verificar

```bash
bash scripts/test-reactive.sh
LD_LIBRARY_PATH="$PWD/.build/reactive-tests" .build/reactive-tests/example
```

Se requiere el compilador Tarn suministrado en `../tarn/target/debug/tarn` o un
`TARN_BIN` explícito, SDK GTK4/json-glib y un display accesible. La prueba compila
el ejemplo real, una fixture, la prueba de estado y el ciclo de vida funcional;
no necesita una compilación previa del contador. `--build-only` omite las ventanas.

La compilación normal sigue en `scripts/build-counter.sh`; la receta fija MCP
también incluye ahora `core/reactive.tarn` en su snapshot de fuentes.

## Verificación realizada

- Prueba Tarn: estado string con propiedad e igualdad, varios cambios, derivación
  par/impar con negativos y límite i64.
- Ciclo de vida funcional nativo: sesiones 20/20, ventanas 19/19, encabezados
  18/18, señales 51/51 y widgets del contador 51/51 liberados. Incluye identidad
  del encabezado, UTF-8 inválido, acceso desde otro hilo y escritura tras cierre.
- Seis pruebas de integración GTK: ejemplo real, no-op confirmado sin render ni
  setters, agrupación inicial, proyección igual sin setters, error/overflow del
  reducer y fallo de vista con estado pendiente y limpieza.
- Regresión del protocolo: 21 pruebas pasan.
- Receta MCP: dos pruebas de compilación y seis escenarios MCP pasan, incluyendo
  build → ejecutar → inspeccionar → clic → reinspeccionar → PNG privado → cerrar.
- Los cuatro negativos de propiedad de sesión siguen rechazados por el compilador.

Diagnósticos y resultados de esta entrega: `.build/reactive-tests/`;
aceptación MCP: `.build/launcher-tests/mcp-build.jsonl`. Se usaron bibliotecas
del sistema, sin aplicar el parche experimental de Fontconfig. La investigación
de sanitizadores está [aplazada y registrada](roadmap.md).

Las [listas dinámicas](dynamic-lists.md) añaden reconciliación de filas con claves
a las vistas de componentes; los árboles arbitrarios siguen pendientes.
