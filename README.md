# Periapsis

A rocket building and orbital exploration simulator in Unreal Engine 5 and C++, in the
spirit of Kerbal Space Program. The orbital mechanics run in SI units and double precision.
Coasting is analytic ("on rails") and powered flight is integrated. Trajectories are
predicted with patched conics across spheres of influence.

**Status:** M4 of 12. You fly a ship around a planet with procedural relief, burn to change
the orbit, time-warp up to 100,000x, reach the moon and land on either body. Near the ship, up
to 4x warp, vessels are simulated by Chaos in a physics bubble with a floating origin, which
turns with the planet near the ground. A spent stage flies 1.5 km ahead of you at the start.
There is no rocket building yet. See [Roadmap](#roadmap).

## Requirements

- Windows 10/11
- Unreal Engine **5.8**
- Visual Studio 2022 with the *Game development with C++* workload (MSVC 14.44 and a
  Windows 10/11 SDK)
- Git LFS (the original specification PDF is stored with it)

The orbital Core has no Unreal dependency: it builds with any C++20 compiler.

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
| `7` / `8` | Hold surface retrograde (for landing) / surface prograde |
| `.` / `,` | Time warp up / down (at most 4x while the engine burns; above 4x the physics bubble switches off) |
| `/` | Back to 1x |
| Right mouse drag | Rotate the camera |
| Mouse wheel | Zoom |
| `Tab` | Cycle the camera focus (ship, spent stage, planet, moon) |
| `R` | Restart the scenario |
| `F1` | Show / hide help |

The HUD shows the current orbit (Ap, Pe, eccentricity, inclination, period) and the
remaining delta-v. It also names the next sphere-of-influence change, shows whether the
physics bubble is on and how far the other vessels are. Type
`` ` `` then `stat fps` to see the frame rate.

### Try it: a landing

Start on the ground with `-Scenario=scenarios/landing_hop.json -CameraDistance=40` added to the
launch command: the ship touches down, settles and shows `LANDED`. Press `5` (radial out) and
`Z` to hop, `X` to cut, and `7` to fall back engine-first.

From orbit: burn retrograde until the periapsis is well below the ground, coast, then hold `7`
(surface retrograde) and brake so that you reach the ground below 12 m/s.

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
Data/               Bodies, terrain profiles and scenarios as JSON, validated on load
Docs/               Technical specification and architecture decision records (adr/)
```

Each layer depends only on the ones before it: Core ← Sim ← Game ← Editor. The simulation
works in meters, kilograms and seconds with `double`. The conversion to Unreal centimeters
happens in a single place, `FUnitBridge`.

## Tests

The tests are kept in a private repository and are not part of this one. There are two
suites: Core tests (CMake + doctest, no Unreal, about two seconds) covering Kepler solvers,
universal-variable propagation, state ↔ elements conversion, maneuvers, patched-conic
prediction, powered flight, the physics bubble's local frames, terrain noise, cube-sphere
meshes and level-of-detail selection; and Unreal Automation tests (prefix `Periapsis.`)
covering data validation, a moon transfer at 100,000x warp, rails/physics transitions, a
descent from orbit to a landing, and Play-In-Editor runs that fly and land in Chaos. Their
paths are listed in `.gitignore`. Each milestone's results are recorded in its ADR.

## Roadmap

The full plan is in the [technical specification](Docs/TechnicalSpecification_v0.1.md).

- [x] **M0** Project structure, modules, CI
- [x] **M1** Orbital Core with tests
- [x] **M2** Spherical planet, point ship with thrust, drawn orbits, warp on rails, basic map
- [x] **M3** Physics bubble: rails-to-physics transition, floating origin, local frame
- [x] **M4** Spherical terrain: cube-sphere with LOD, local collision, landing
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

[MIT](LICENSE). Unreal Engine is not part of this repository; building and shipping the
project requires accepting the Unreal Engine End User License Agreement from Epic Games.
