# ADR 0002 — Prototipo orbital (M2)

Fecha: 2026-10-15. Estado: aceptada; la validación visual y de FPS la hace una persona.

## Rieles exactos, empuje integrado

`Periapsis::Core::Flight` avanza la nave de dos formas:

- **Sin empuje (sobre rieles):** evalúa las cónicas que predice `PredictTrajectory`. Es
  analítico, así que cualquier warp da el mismo resultado; lo prueban los tests, con
  diferencias menores a 1e-9 entre 5 s y 1667 s por paso, flyby lunar incluido. Al llegar al
  final de un parche, cambia de SOI con `ChangeFrame` y sigue con el parche siguiente.
- **Con empuje:** RK4 en pasos fijos de 1/120 s, con la masa que baja dentro del paso. Al
  cortar el motor se vuelve a predecir desde ese estado. Los cambios de SOI con motor se
  detectan por distancia después de cada paso.

## Límites de warp (en `FPeriapsisUniverse`)

- Niveles: 1, 2, 4, 10, 50, 100, 1.000, 10.000 y 100.000x.
- Con el motor encendido, como máximo 4x.
- Antes de un cambio de SOI o de un impacto, el warp baja hasta que queden al menos 8
  cuadros para el evento. La física no lo necesita, porque los rieles son exactos; es para
  que en pantalla no haya un salto de un cuadro a través del encuentro.

## Datos

- `Data/bodies/home_system.json` describe el sistema: el planeta mide 1.000 km y tiene
  g = 9,81 (spec 6.2), y la luna mide 200 km, tiene g = 1,63 y orbita a 12.000 km. `mu` se
  deriva como `g R^2`.
- `Data/scenarios/orbital_prototype.json` define la nave y la órbita inicial.
- El cargador (`PeriapsisData`) valida los datos y rechaza el archivo entero, sin cargarlo a
  medias, con un error legible por cada problema.
- Para empaquetar, `Data/` se copia fuera del pak (`DefaultGame.ini`). Todavía no se
  empaquetó.

## Vista centrada en el foco

`APeriapsisUniverseView` dibuja todo en relación con el foco (la nave o un cuerpo), que
queda siempre en el origen de Unreal. Así, la precisión no depende de dónde esté la acción.
La escala es real: 1 m equivale a 100 cm por `FUnitBridge`, sin una escala aparte de mapa.

- Los cuerpos son la esfera del motor (`/Engine/BasicShapes`) con un material dinámico.
- Órbitas, trayectoria, Ap/Pe, SOI y nave se dibujan con `DrawDebug*`, que en Shipping
  desaparece. Se reemplaza al hacer la UI real (M7).
- Cada parche de la trayectoria se dibuja alrededor de la posición actual de su cuerpo,
  como en KSP.

## Sin assets

Para no editar binarios, no se usa ningún asset propio:

- El mapa es `/Engine/Maps/Entry`, que está vacío.
- `APeriapsisGameMode` crea la vista y la cámara desde el código, sin PlayerStart.
- La entrada se lee consultando las teclas en `PlayerTick`. Enhanced Input necesita
  assets; se adopta cuando haya un mapa real.
- La exposición es manual (post-proceso sin límites) y la luz es direccional, desde la vista.

## Prueba en pantalla (2026-10-15)

Se jugó en modo juego (`-game`, ventana de 1600x900, render al 83 %) con `stat unit`:

- **FPS:** 155 a 165 en órbita, a cualquier warp hasta 100.000x, y 135 a 150 con el motor
  encendido. En el editor, con la ventana en segundo plano, cae a unos 6 FPS por la
  limitación de CPU del editor; no es un problema del juego.
- **Transferencia a la luna:** se hizo a mano. El warp baja solo antes del encuentro, la
  nave entra en la SOI justo en el borde (1.414 km de altitud con una SOI de unos 1.420 km)
  y la vista muestra la hipérbola del sobrevuelo y el parche siguiente.

Esa prueba encontró tres errores, ya corregidos y con test:

- **Escape del cuerpo raíz:** al pasar a una órbita abierta, cada predicción muestreaba
  1e9 s (`OpenOrbitSpan`) y tardaba 6 s, y el juego caía a 2 FPS. Ahora el muestreo da pasos
  largos mientras la nave está más allá del alcance de todos los hijos, con la cota de
  velocidad que da la energía. Lo mismo vale para las órbitas cerradas que llegan muy lejos.
- **`PropagateKepler` en hipérbolas:** a tiempos largos (más de unos 1e5 s), Newton avanzaba
  de a pasos fijos sobre una función exponencial, se le acababan las iteraciones y devolvía
  estados con r ~ 1e111. `SolveMonotonic` ahora bisecta cuando Newton no achica el paso a la
  mitad (como `rtsafe`).
- **Etiquetas congeladas:** `DrawDebugString` con duración -1 deja el texto para siempre en
  el HUD. Las etiquetas se acumulaban hasta el tope por actor y después las nuevas se
  descartaban. Ahora usan duración 0, que las dibuja un solo cuadro.

## Pendiente o a revisar

- **Orientación de los ejes:** el Core usa ejes dextrógiros y `FUnitBridge` copia X, Y y Z
  tal cual al sistema levógiro de Unreal. En pantalla, las órbitas prograde se ven en
  sentido horario desde +Z. Decidir antes de M3 si `FUnitBridge` invierte Y.
- **Predicción con el motor encendido:** se recalcula en cada cuadro. Con el motor encendido,
  el hilo de juego tarda entre 4 y 7 ms; sin motor, entre 3 y 4 ms. Si crece (más cuerpos),
  pasarla a un hilo de trabajo (`PredictTrajectory` solo lee) o limitarla a unas 10 Hz.
- **Las esferas del motor tienen pocas caras:** de cerca se ven las facetas del planeta.
  Se resuelve con una malla propia cuando haya assets (M3 o después).
