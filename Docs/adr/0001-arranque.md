# ADR 0001 — Decisiones de arranque (M0 + M1)

Fecha: 2026-10-15. Estado: aceptada.

## Nombre: `Periapsis`

La spec deja el nombre abierto (decisión 1) y usa `PROYECTO`. Los módulos de UE necesitan
un nombre desde el día uno y renombrarlos más tarde cuesta redirects de clases y assets.
Arrancó como `SpaceSim` (provisorio) y se cambió a `Periapsis` el 2026-10-15, antes de la
primera compilación y sin assets, así que no hizo falta ningún redirect. Módulos:
`PeriapsisCore`, `PeriapsisSim`, `PeriapsisGame`, `PeriapsisEditor`; namespace del Core
`Periapsis::Core`. La carpeta del repo sigue llamándose `spacesim`. No se verificaron
marcas ni disponibilidad en tiendas.

## El Core depende del módulo `Core` de UE, solo para registrarse

La spec dice "Dependencias: ninguna". Un módulo de UE necesita `IMPLEMENT_MODULE`, que vive
en el módulo `Core` del motor. Se aisló en `PeriapsisCoreModule.cpp`, que el CMake de los
tests excluye; el resto del Core es C++ estándar y compila sin el motor (lo prueba el CI).
`PCHUsage = NoPCHs` para que ningún archivo del Core reciba headers de UE por PCH.

Para que Sim pueda llamar al Core desde otra DLL, las funciones públicas llevan
`PERIAPSISCORE_API`. UBT la define como `DLLEXPORT`/`DLLIMPORT`, que son macros de las
cabeceras de plataforma de UE. `PeriapsisCore/Api.h` las define con el mismo texto que usa UE
(redefinirlas igual es legal) y, fuera de UE, deja `PERIAPSISCORE_API` vacía. Las plantillas
y las funciones `inline` no la necesitan. Si falta en una función nueva, el síntoma es
`LNK2019` en el módulo que la llama; los tests de CMake no lo detectan porque linkean
estático. Lo cubre `Periapsis.Sim.CoreLink`.

## Semi-latus rectum en vez de semieje mayor

`OrbitalElements` guarda `p` y no `a`: `a` es infinito en una parabólica. `a` se deriva
(`SemiMajorAxis`) y los datos con `a` y anomalía media entran por `ElementsFromClassical`.

## Convenciones de órbitas degeneradas

Ecuatorial: nodo en +X (`LongitudeOfAscendingNode = 0`). Circular: periapsis en el nodo
(`ArgumentOfPeriapsis = 0`) y la anomalía verdadera pasa a ser argumento de latitud o
longitud verdadera. Umbrales `1e-11`. Con eso la ida y vuelta estado→elementos→estado es
exacta también en los casos degenerados.

## Propagación con variables universales

Un solo camino para elípticas, parabólicas e hiperbólicas (Stumpff con serie cerca de 0).
La ecuación se resuelve con Newton salvaguardado por bisección: `F'(chi) = r > 0`, así que
siempre converge. Las elípticas reducen `dt` módulo el período antes de resolver.

## Física asíncrona vs sub-stepping

La spec pide los dos. En Chaos, con `bTickPhysicsAsync` activo el sub-stepping no se usa.
`Config/DefaultEngine.ini` deja async a 120 Hz y los valores de sub-stepping escritos. A
verificar en M3 con la versión de UE que se fije.

## Entorno de desarrollo (resuelto el 2026-10-15)

- **UE 5.8.3**, instalado con el launcher en `H:\Epic Games\UE_5.8`. Es un HDD: lento, pero
  hay espacio. El `.uproject` dice `"EngineAssociation": "5.8"`.
- **Visual Studio 2022 Community** en `H:\VisualStudio\2022\Community`, con MSVC 14.44 y el
  SDK de Windows 10.0.22621.
- **Cachés en `S:`** (SSD), configuradas por usuario y fuera del repo:
  - Zen (la DDC de UE 5.8): `[Zen.AutoLaunch] DataPath=S:/UE-Zen` en
    `%LOCALAPPDATA%\Unreal Engine\Engine\Config\UserEngine.ini`.
  - UBA: `RootDir=S:\UBA` en `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`.
  - La variable `UE-LocalDataCachePath` ya no la usa UE 5.8 con Zen.
- La primera compilación limpia de `PeriapsisEditor` tardó ~4,5 min (Ryzen 5 2600) y salió
  sin warnings.
