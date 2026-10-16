# ADR 0002 — Orbital prototype (M2)

Date: 2026-10-15. Status: accepted; visual and FPS validation is done by a person.

## Exact rails, integrated thrust

`Periapsis::Core::Flight` advances the ship in two ways:

- **No thrust (on rails):** it evaluates the conics predicted by `PredictTrajectory`. This is
  analytic, so any warp gives the same result; the tests prove it, with differences below
  1e-9 between 5 s and 1667 s per step, lunar flyby included. At the end of a patch it
  changes SOI with `ChangeFrame` and continues on the next patch.
- **Thrust:** RK4 in fixed 1/120 s steps, with mass decreasing within the step. When the
  engine cuts off, the trajectory is predicted again from that state. SOI changes under
  thrust are detected by distance after each step.

## Warp limits (in `FPeriapsisUniverse`)

- Levels: 1, 2, 4, 10, 50, 100, 1,000, 10,000 and 100,000x.
- At most 4x with the engine on.
- Before an SOI change or an impact, the warp drops until at least 8 frames remain before the
  event. The physics doesn't need it, because the rails are exact; it keeps the screen from
  jumping across the encounter in a single frame.

## Data

- `Data/bodies/home_system.json` describes the system: the planet has a 1,000 km radius and
  g = 9.81 (spec 6.2); the moon has a 200 km radius, g = 1.63 and orbits at 12,000 km. `mu` is
  derived as `g R^2`.
- `Data/scenarios/orbital_prototype.json` defines the ship and its starting orbit.
- The loader (`PeriapsisData`) validates the data and rejects the whole file, never loading
  it halfway, with a readable error for each problem.
- For packaging, `Data/` is staged outside the pak (`DefaultGame.ini`). Nothing has been
  packaged yet.

## Focus-centered view

`APeriapsisUniverseView` draws everything relative to the focus (the ship or a body), which
always sits at the Unreal origin. That way precision doesn't depend on where the action is.
The scale is real: 1 m is 100 cm through `FUnitBridge`, with no separate map scale.

- Bodies are the engine sphere (`/Engine/BasicShapes`) with a dynamic material.
- Orbits, trajectory, Ap/Pe, SOI and the ship are drawn with `DrawDebug*`, which disappears in
  Shipping. It gets replaced when the real UI is built (M7).
- Each trajectory patch is drawn around the current position of its body, as in KSP.

## No assets

To avoid editing binaries, no project assets are used:

- The map is `/Engine/Maps/Entry`, which is empty.
- `APeriapsisGameMode` creates the view and the camera from code, with no PlayerStart.
- Input is read by polling keys in `PlayerTick`. Enhanced Input needs assets; it will be
  adopted once there is a real map.
- Exposure is manual (unbound post-process volume) and the light is directional, from the
  view.

## On-screen test (2026-10-15)

Played in game mode (`-game`, 1600x900 window, 83 % render resolution) with `stat unit`:

- **FPS:** 155 to 165 in orbit at any warp up to 100,000x, and 135 to 150 with the engine on.
  In the editor, with the window in the background, it drops to about 6 FPS because the editor
  throttles its CPU use; that is not a game problem.
- **Transfer to the moon:** flown by hand. The warp drops on its own before the encounter, the
  ship enters the SOI right at its edge (1,414 km altitude for an SOI of about 1,420 km) and
  the view shows the flyby hyperbola and the following patch.

That test found three bugs, now fixed and covered by tests:

- **Escaping the root body:** on an open orbit, every prediction sampled 1e9 s
  (`OpenOrbitSpan`) and took 6 s, and the game fell to 2 FPS. Now the sampling takes long
  steps while the ship is beyond the reach of every child body, using the speed bound given by
  the energy. The same applies to closed orbits that reach very far out.
- **`PropagateKepler` on hyperbolas:** at long times (beyond about 1e5 s), Newton crawled in
  fixed steps over an exponential function, ran out of iterations and returned states with
  r ~ 1e111. `SolveMonotonic` now bisects when Newton doesn't halve the step (as in `rtsafe`).
- **Frozen labels:** `DrawDebugString` with a duration of -1 keeps the text in the HUD
  forever. The labels piled up to the per-actor cap, and new ones were then discarded. They
  now use a duration of 0, which draws them for a single frame.

## Pending or to review

- **Axis handedness:** resolved in ADR 0003: `FUnitBridge` mirrors Y.
- **Prediction with the engine on:** it is recomputed every frame. With the engine on, the
  game thread takes 4 to 7 ms; without it, 3 to 4 ms. If it grows (more bodies), move it to a
  worker thread (`PredictTrajectory` only reads) or limit it to about 10 Hz.
- **The engine spheres have few faces:** up close, the planet's facets show. To be solved
  with a proper mesh once there are assets (M3 or later).
