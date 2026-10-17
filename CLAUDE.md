# Periapsis

Rocket building and orbital exploration simulator in Unreal Engine 5 + C++.
The specification is `Docs/TechnicalSpecification_v0.1.md` (the original, in Spanish, is
`Docs/Especificacion_Tecnica_v0.1.pdf`). Decisions that deviate from it or refine it go in
`Docs/adr/`.

## Hard rules

- **Everything in the repository is in English:** code, comments, names, docs, data and
  commit messages. Only the conversation with the user is in Spanish.
- **C++ first.** Blueprints only for thin glue (UI, materials).
- **Unreal binaries are never edited by hand** (`.uasset`, `.umap`, textures, meshes): they
  can't be reviewed in Git. A binary is allowed only as the output of a script in `Tools/Assets/`
  that is committed with it and regenerates it; the script is the source and is what gets
  reviewed (ADR 0006). Part meshes stay procedural C++ (mass and collision come from the same
  data). Whatever can be expressed as data goes in `Data/`.
- **SI units and `double`** throughout the simulation (m, kg, s, N). The conversion to Unreal
  centimeters lives **only** in `FUnitBridge` (`Source/PeriapsisSim/Public/UnitBridge.h`).
- **The Core includes no Unreal headers.** `Source/PeriapsisCore` is pure standard C++; the
  only exception is `PeriapsisCoreModule.cpp` (module registration), which the tests exclude
  by name (`*Module.cpp`). If a Core file needs something from UE, it goes in Sim.
- **Every non-inline Core function carries `PERIAPSISCORE_API`** (from
  `PeriapsisCore/Api.h`). Without it the CMake tests still pass, but Sim fails with `LNK2019`.
- **Layers:** Core ← Sim ← Game ← Editor. Each layer depends only on the ones below.
- **The state vector is the source of truth** for an orbit; elements are derived.

## Conventions

- Outside the Core: Unreal prefixes (`A`, `U`, `F`, `E`, `I`, `T`, `b` for bools).
- In the Core: namespace `Periapsis::Core`, unprefixed types (`Vec3`, `StateVector`) so they
  don't clash with Unreal's (`FVector3d`). PascalCase like Unreal.
- Never use `PI`, `check`, `verify` or `ensure` as identifiers: they are Unreal macros.
- No exceptions or RTTI in the Core (Unreal builds without them).
- Shadowing variables is an error in Unreal; the test CMake replicates it (`/w14456...`,
  `-Wshadow`).

## Commands

The repository is public (MIT) and the tests are not in it: they live in the private
repository `Agustin-Delgado/periapsis-heldout` and are copied over the checkout (their paths are
in `.gitignore`, so they can stay there). Never commit them here. New tests go to that
repository, at the same paths.

Core tests (no Unreal, ~2 s). `cmake` is not on the PATH: the script uses the one bundled
with Visual Studio Build Tools 2022.

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/run-core-tests.ps1
```

Build the editor (~4.5 min the first time, seconds afterwards) and run the integration tests
(Automation, prefix `Periapsis.`). UE 5.8.3 is in `H:\Epic Games\UE_5.8`. Test results end up
in `Saved/Logs/Periapsis.log`; search for `Test Completed`.

```bat
"H:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" PeriapsisEditor Win64 Development -Project="%CD%\Periapsis.uproject" -WaitMutex
"H:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Periapsis.uproject" -ExecCmds="Automation RunTests Periapsis; Quit" -unattended -nopause -NullRHI -nosplash
```

Don't build while the editor or the game is open: Live Coding and the loaded DLLs block the
build.

CI lives in `periapsis-heldout`: it checks out this repository, lays the tests over it and runs
the Core tests on Windows and Linux.

## Definition of done

It builds, tests are green, no new warnings, data is validated. Everything measurable
(orbits, delta-v, conversions, loads) becomes a test.

## Agent limits

The agent sees the viewport only through screenshots it takes itself (warning the user first),
so the final judgement on visuals is a person's: the agent leaves debug lines and text and clear
logs. There is no artist: parts, materials, sky and effects are made by the agent in code and
generator scripts; textures can come from Quixel Megascans (licensed for use in Unreal).

## Status

- M0: structure, modules, CI and this file. The editor builds without warnings on UE 5.8.3,
  loads the 4 modules and passes the `Periapsis.Sim.*` Automation tests. The environment and
  caches are described in `Docs/adr/0001-bootstrap.md`.
- M1: complete orbital Core with tests (`Tools/CoreTests`).
- M2: playable orbital prototype (details in `Docs/adr/0002-orbital-prototype.md`). The
  automated part is green: the `Periapsis.Sim.Mission.TransferToTheMoon` test flies the
  transfer to the moon at 100,000x warp. Play-tested on 2026-10-15: 135 to 165 FPS and the
  transfer to the moon flown by hand (details in the ADR). Controls are shown on screen (F1).
- M3: physics bubble on Chaos with a free-falling local frame, floating origin and bubble
  membership (details in `Docs/adr/0003-physics-bubble.md`). `FUnitBridge` mirrors Y. The
  rails/physics jump is 1e-13 m/s; Chaos matches the Core's integration to under 1 mm. The
  `Periapsis.Editor.*` tests run real Chaos in PIE.
- M4: spherical terrain (noise in data, cube-sphere with error-driven detail, worker
  threads), body rotation, a body-fixed physics frame near the ground, collision patch,
  impacts, rest and landing (details in `Docs/adr/0004-spherical-terrain.md`). Launch with
  `-Scenario=scenarios/landing_hop.json -CameraDistance=40` to start on the ground.
- M5: vessels made of parts (details in `Docs/adr/0005-vessels-with-parts.md`): parts and
  designs in `Data/parts` and `Data/vessels`, propellant domains, staging with separation,
  joint loads, delta-v per stage, one compound Chaos body per vessel, and a keyboard builder
  (`-Builder`, or `B` in flight). The 54-part Trident matches the rocket equation worked out by
  hand and delivers it in flight. `-Scenario=scenarios/launch_pad.json` starts on the pad.
