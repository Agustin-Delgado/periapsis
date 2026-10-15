# ADR 0001 — Bootstrap decisions (M0 + M1)

Date: 2026-10-15. Status: accepted.

## Name: `Periapsis`

The spec leaves the name open (decision 1) and uses `PROYECTO` as a placeholder. Unreal
modules need a name from day one, and renaming them later costs class and asset redirects.
The project started as `SpaceSim` (provisional) and was renamed `Periapsis` on 2026-10-15,
before the first build and with no assets, so no redirects were needed. Modules:
`PeriapsisCore`, `PeriapsisSim`, `PeriapsisGame`, `PeriapsisEditor`; the Core namespace is
`Periapsis::Core`. The repository folder is still called `spacesim`. Trademarks and store
availability were not checked.

## The Core depends on UE's `Core` module, only to register itself

The spec says "Dependencies: none". An Unreal module needs `IMPLEMENT_MODULE`, which lives in
the engine's `Core` module. It is isolated in `PeriapsisCoreModule.cpp`, which the test CMake
excludes; the rest of the Core is standard C++ and builds without the engine (CI proves it).
`PCHUsage = NoPCHs` so that no Core file receives Unreal headers through a PCH.

So that Sim can call the Core across DLLs, public functions carry `PERIAPSISCORE_API`. UBT
defines it as `DLLEXPORT`/`DLLIMPORT`, which are macros from Unreal's platform headers.
`PeriapsisCore/Api.h` defines them with the same text Unreal uses (an identical redefinition
is legal) and, outside Unreal, leaves `PERIAPSISCORE_API` empty. Templates and `inline`
functions don't need it. If a new function lacks it, the symptom is `LNK2019` in the calling
module; the CMake tests don't catch it because they link statically. `Periapsis.Sim.CoreLink`
covers it.

## Semi-latus rectum instead of semi-major axis

`OrbitalElements` stores `p`, not `a`: `a` is infinite on a parabola. `a` is derived
(`SemiMajorAxis`), and data given as `a` plus mean anomaly comes in through
`ElementsFromClassical`.

## Conventions for degenerate orbits

Equatorial: the node is on +X (`LongitudeOfAscendingNode = 0`). Circular: periapsis is on the
node (`ArgumentOfPeriapsis = 0`) and the true anomaly becomes the argument of latitude or the
true longitude. Thresholds are `1e-11`. With this, the round trip state → elements → state is
exact in the degenerate cases too.

## Universal-variable propagation

One path for elliptic, parabolic and hyperbolic orbits (Stumpff functions, with a series near
0). The equation is solved with Newton safeguarded by bisection: `F'(chi) = r > 0`, so it
always converges. Elliptic orbits reduce `dt` modulo the period before solving.

## Async physics vs sub-stepping

The spec asks for both. In Chaos, sub-stepping is not used when `bTickPhysicsAsync` is on.
`Config/DefaultEngine.ini` leaves async at 120 Hz with the sub-stepping values written down.
To be verified in M3 with the pinned UE version.

## Development environment (settled on 2026-10-15)

- **UE 5.8.3**, installed with the launcher in `H:\Epic Games\UE_5.8`. It is an HDD: slow, but
  there is room. The `.uproject` says `"EngineAssociation": "5.8"`.
- **Visual Studio 2022 Community** in `H:\VisualStudio\2022\Community`, with MSVC 14.44 and
  Windows SDK 10.0.22621.
- **Caches on `S:`** (SSD), configured per user and outside the repository:
  - Zen (UE 5.8's DDC): `[Zen.AutoLaunch] DataPath=S:/UE-Zen` in
    `%LOCALAPPDATA%\Unreal Engine\Engine\Config\UserEngine.ini`.
  - UBA: `RootDir=S:\UBA` in `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`.
  - UE 5.8 with Zen no longer uses the `UE-LocalDataCachePath` variable.
- The first clean build of `PeriapsisEditor` took about 4.5 min (Ryzen 5 2600) with no
  warnings.
