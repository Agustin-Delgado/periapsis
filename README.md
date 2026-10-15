# Periapsis

[![Core tests](https://github.com/Agustin-Delgado/periapsis/actions/workflows/core-tests.yml/badge.svg)](https://github.com/Agustin-Delgado/periapsis/actions/workflows/core-tests.yml)

A rocket building and orbital exploration simulator in Unreal Engine 5 and C++, in the
spirit of Kerbal Space Program. The orbital mechanics run in SI units and double precision.
Coasting is analytic ("on rails") and powered flight is integrated. Trajectories are
predicted with patched conics across spheres of influence.

**Status:** M2 of 12, a playable orbital prototype. You fly a point ship around a planet,
burn to change the orbit, time-warp up to 100,000x and reach the moon. There is no rocket
building yet. See [Roadmap](#roadmap).

## Requirements

- Windows 10/11
- Unreal Engine **5.8**
- Visual Studio 2022 with the *Game development with C++* workload (MSVC 14.44 and a
  Windows 10/11 SDK)
- Git LFS (the original specification PDF is stored with it)

The orbital Core has no Unreal dependency. Its tests need only a C++20 compiler and CMake,
on Windows or Linux.

## Getting started

```bat
git clone https://github.com/Agustin-Delgado/periapsis.git
cd periapsis
```

Double-click `Periapsis.uproject` and accept rebuilding the modules. The first launch
compiles the engine shaders, which can take a while. To build from the command line
instead:

```bat
"<UE_5.8>\Engine\Build\BatchFiles\Build.bat" PeriapsisEditor Win64 Development -Project="%CD%\Periapsis.uproject" -WaitMutex
```

The editor opens an empty map. Everything is spawned from code when you press **Play**
(`Alt+P`). Click the viewport so it receives the keyboard.

You can also run it as a standalone window:

```bat
"<UE_5.8>\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\Periapsis.uproject" -game -windowed -ResX=1600 -ResY=900
```

## Controls

| Key | Action |
|---|---|
| `Shift` / `Ctrl` | Throttle up / down |
| `Z` / `X` | Full throttle / cut engine |
| `1`–`6` | Hold prograde, retrograde, normal, anti-normal, radial out, radial in |
| `.` / `,` | Time warp up / down (at most 4x while the engine burns) |
| `/` | Back to 1x |
| Right mouse drag | Rotate the camera |
| Mouse wheel | Zoom |
| `Tab` | Cycle the camera focus (ship, planet, moon) |
| `R` | Restart the scenario |
| `F1` | Show / hide help |

The HUD shows the current orbit (Ap, Pe, eccentricity, inclination, period) and the
remaining delta-v. It also names the next sphere-of-influence change. Type
`` ` `` then `stat fps` to see the frame rate.

### Try it: a transfer to the moon

1. Zoom out until the moon's orbit (gray) is visible.
2. Warp until the moon leads the ship by roughly 100–110°, then drop back to 1x.
3. Press `1` (prograde) and `Z`. The ship's orbit (blue) stretches out. Cut with `X` once
   its apoapsis reaches the moon's orbit and a `SOI -> Moon` marker appears.
4. Warp. The warp drops by itself before the encounter, and the HUD switches to
   *Orbiting Moon*.

## Project layout

```
Source/
  PeriapsisCore/    Orbital mechanics in pure standard C++ (no Unreal headers)
  PeriapsisSim/     Bridge to Unreal: data loading, universe, warp, unit conversion
  PeriapsisGame/    Game mode, focus-centered view, camera, HUD, input
  PeriapsisEditor/  Editor-only code and Play-In-Editor tests
Data/               Bodies and scenarios as JSON, validated on load
Tools/CoreTests/    CMake + doctest suite for the Core, runs without Unreal
Scripts/            Helper scripts
Docs/               Technical specification and architecture decision records (adr/)
```

Each layer depends only on the ones before it: Core ← Sim ← Game ← Editor. The simulation
works in meters, kilograms and seconds with `double`. The conversion to Unreal centimeters
happens in a single place, `FUnitBridge`.

## Tests

Core tests run without Unreal in about a second:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/run-core-tests.ps1
```

They cover Kepler solvers, universal-variable propagation, state ↔ elements conversion,
maneuvers, patched-conic prediction and powered flight. CI runs them on Windows and Linux on
every push.

Integration tests use Unreal's Automation framework (prefix `Periapsis.`). They include a
full moon transfer flown at 100,000x warp and a Play-In-Editor smoke test:

```bat
"<UE_5.8>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Periapsis.uproject" -ExecCmds="Automation RunTests Periapsis; Quit" -unattended -nopause -NullRHI -nosplash
```

Results are written to `Saved/Logs/Periapsis.log`.

## Roadmap

The full plan is in the [technical specification](Docs/TechnicalSpecification_v0.1.md).

- [x] **M0** Project structure, modules, CI
- [x] **M1** Orbital Core with tests
- [x] **M2** Spherical planet, point ship with thrust, drawn orbits, warp on rails, basic map
- [ ] **M3** Physics bubble: rails-to-physics transition, floating origin, local frame
- [ ] **M4** Spherical terrain: cube-sphere with LOD, local collision, landing
- [ ] **M5** Ships made of parts: builder, parts, tanks, engines, stages, logical structure, delta-v
- [ ] **M6** Atmosphere and reentry: density, drag, heating, parachutes, Sky Atmosphere
- [ ] **M7** Map and planning: full map, maneuver nodes, encounter markers, SOI transitions in the UI
- [ ] **M8** Full system: 6 to 8 bodies in data, multiple SOIs, scale tuning
- [ ] **M9** Campaign: tech tree, budget, contracts, crew, versioned saves
- [ ] **M10** Story and content: events, dialogues, discoveries, rival agency, final content
- [ ] **M11** Polish and launch: performance, art, audio, localization, QA, Early Access

Design decisions that refine or deviate from the spec are recorded in
[`Docs/adr/`](Docs/adr/).

## License

Proprietary, all rights reserved. Periapsis is meant to become a commercial game, so no
open-source license is granted. See [LICENSE](LICENSE). Third-party components keep their
own licenses (doctest: MIT).
