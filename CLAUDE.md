# Periapsis

Simulador de construcción de cohetes y exploración orbital en Unreal Engine 5 + C++.
La especificación es `Docs/Especificacion_Tecnica_v0.1.pdf` (texto plano al lado, `.txt`,
para leerla sin PDF). Las decisiones que la desvían o la precisan van en `Docs/adr/`.

## Reglas duras

- **C++ primero.** Blueprints solo para pegamento fino (UI, materiales).
- **No editar binarios de Unreal** (`.uasset`, `.umap`, texturas, mallas): no se pueden
  revisar en Git ni editar bien desde un agente. Lo que se pueda expresar como dato va a `Data/`.
- **Unidades SI y `double`** en toda la simulación (m, kg, s, N). La conversión a
  centímetros de Unreal vive **solo** en `FUnitBridge` (`Source/PeriapsisSim/Public/UnitBridge.h`).
- **El Core no incluye headers de Unreal.** `Source/PeriapsisCore` es C++ estándar puro; la
  única excepción es `PeriapsisCoreModule.cpp` (registro del módulo), que los tests excluyen
  por nombre (`*Module.cpp`). Si un archivo del Core necesita algo de UE, va en Sim.
- **Toda función no inline del Core lleva `PERIAPSISCORE_API`** (de `PeriapsisCore/Api.h`).
  Sin eso, los tests de CMake pasan igual, pero Sim falla con `LNK2019`.
- **Capas:** Core ← Sim ← Game ← Editor. Cada capa depende solo de las de abajo.
- **El vector de estado es la fuente de verdad** de una órbita; los elementos se derivan.
- **Código, comentarios y nombres en inglés.** Conversación y commits en español.

## Convenciones

- Fuera del Core: prefijos de UE (`A`, `U`, `F`, `E`, `I`, `T`, `b` para bools).
- En el Core: namespace `Periapsis::Core`, tipos sin prefijo (`Vec3`, `StateVector`) para no
  chocar con los de UE (`FVector3d`). PascalCase igual que UE.
- Nada de `PI`, `check`, `verify`, `ensure` como identificadores: son macros de UE.
- Sin excepciones ni RTTI en el Core (UE compila sin ellas).
- Sombrear variables es error en UE; el CMake de los tests lo replica (`/w14456...`, `-Wshadow`).

## Comandos

Tests del Core (sin UE, ~1 s). `cmake` no está en el PATH: el script usa el que trae
Visual Studio Build Tools 2022.

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/run-core-tests.ps1
```

Compilar el editor (~4,5 min la primera vez, segundos después) y correr los tests de
integración (Automation, prefijo `Periapsis.`). UE 5.8.3 está en `H:\Epic Games\UE_5.8`.
El resultado de los tests queda en `Saved/Logs/Periapsis.log`; buscar `Test Completed`.

```bat
"H:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" PeriapsisEditor Win64 Development -Project="%CD%\Periapsis.uproject" -WaitMutex
"H:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Periapsis.uproject" -ExecCmds="Automation RunTests Periapsis; Quit" -unattended -nopause -NullRHI -nosplash
```

No compilar mientras el editor está abierto: Live Coding bloquea el build.

CI (`.github/workflows/core-tests.yml`) corre los tests del Core en Windows y Linux.

## Definición de "hecho"

Compila, tests verdes, sin advertencias nuevas, datos validados. Todo lo medible
(órbitas, delta-v, conversiones, cargas) se convierte en test.

## Límites del agente

No ve el viewport. Lo visual lo valida una persona: el agente deja líneas y texto de
depuración y logs claros. El ensamblado de niveles, materiales y Niagara lo hace una persona.

## Estado

- M0: estructura, módulos, CI y este archivo. El editor compila sin warnings con UE 5.8.3,
  carga los 4 módulos y pasa los tests Automation `Periapsis.Sim.*`. El entorno y las
  cachés se describen en `Docs/adr/0001-arranque.md`.
- M1: Core orbital completo con tests (`Tools/CoreTests`).
