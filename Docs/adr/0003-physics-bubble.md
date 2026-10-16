# ADR 0003 — Physics bubble (M3)

Date: 2026-10-16. Status: accepted; the visual check (no jitter on screen) is done by a person.

## Mirrored Y in `FUnitBridge`

ADR 0002 left open whether `FUnitBridge` should flip an axis. It does now: the simulation is
right-handed and Unreal left-handed, so Y is mirrored both ways. A prograde orbit
(angular momentum along +Z) turns counter-clockwise seen from above, on screen as in the
simulation. Positions, velocities, accelerations, forces and directions convert with the
bridge (`ToUnreal`, `ToSI`, `DirectionToUnreal`). Axial vectors (angular velocity, torque)
would take an extra sign; none cross the bridge yet, and rotations are built on the Unreal
side from converted directions.

## A free-falling local frame

The spec (3.4 d) asks for a local physics frame with its origin at the active vessel. Its
origin (`Periapsis::Core::LocalFrame`) **free-falls on a Kepler orbit** around the active
vessel's body and does not rotate. Consequences:

- A vessel coasting with the origin stays nearly still in the frame, whatever its orbital
  speed. Chaos only sees positions within a few kilometres and speeds within ~100 m/s, so
  the scene stays near the origin as the spec requires (3.5). We never rely on double
  precision inside Chaos.
- Gravity in the frame is the difference between the pull on the object and on the origin
  (`RelativeGravity`), evaluated at mid-step (`StepGravity`) so a held acceleration is
  second-order accurate. Chaos gets no gravity, damping or sleeping of its own.
- Converting between body-relative and local states is a translation in double precision:
  the transition between rails and physics adds no error. Measured jump: 1e-13 m/s
  (criterion: below 1e-3 m/s).
- Crossing a sphere of influence re-expresses the origin around the new body
  (`ChangeBody`); local coordinates keep their meaning.

The spec's alternative near the surface (a frame rotating with the body, with centrifugal and
Coriolis terms) is deferred to M4, when there is a surface to land on.

## Floating origin

The origin is moved onto the active vessel when it is more than **5 km** away (spec 3.5) or
more than **100 m/s** faster than it (a burn builds up speed in the frame). Every simulated
body is shifted by the same amount in the physics step. The view anchors everything to where
Chaos *shows* the ship, so a rebase does not show up as a jump, whatever Chaos's
interpolation does with the teleported bodies.

Criterion "vessel 1,000 km from the origin without visible jitter": the ship never gets more
than 5 km from the physics origin. The Core test that integrates an object over 1,060 km of
travel in the frame, with rebases, ends 6e-7 m from its exact Kepler position. Rendering far
from the origin (camera on a body) relies on Large World Coordinates.

## Bubble membership

Other vessels enter the bubble closer than **2.2 km** to the active one and leave beyond
**2.5 km**. The spec reads "2.5 km on entry, 2.2 km on exit", which taken literally has no
hysteresis (a vessel at 2.3 km would enter and leave every step); we read it the other way
round, as KSP does. The scenario gains `other_vessels` (passive vessels with a mass and a
circular start orbit); the prototype puts a spent stage 1.5 km ahead of the ship.

## Rails or physics

- The bubble runs when a physics scene is attached and the warp is at most **4x**; above that
  everything goes on rails (spec 3.3). The bubble's warp is the world's time dilation, so
  Chaos takes 1 to 4 fixed steps of 1/120 s per 1/120 s of real time.
- `FPeriapsisUniverse::PhysicsStep` runs before every Chaos step, through
  `AsyncPhysicsTickActor` on `APeriapsisBubble` (a callback on the frozen game thread, so no
  locking). It reads the bodies, moves the origin if needed, updates membership, burns fuel
  (`Flight::BurnFor`) and sets the accelerations for the step. Bodies are only created and
  destroyed on the next game-thread tick; until Chaos simulates a body, its vessel coasts on
  rails, so nothing waits.
- Leaving the bubble happens inside a physics step, so it uses the exact state Chaos
  computed, not the interpolated one on screen.
- While the physics state follows the predicted trajectory (within 1 cm and 1e-4 m/s), the
  prediction is kept: coasting in the bubble does not re-predict every step.
- Without a scene (tests, headless use) everything works as in M2: the Core integrates thrust
  with RK4. `FPeriapsisPointMassScene` stands in for Chaos in the Sim tests.

## Chaos details

- Chaos steps with semi-implicit Euler, which overshoots the position by a dt²/2 per step
  under a constant acceleration (0.7 m over a 30 s burn). The scene starts each step that much
  behind, which makes the step exact: the same 32 s burn in Chaos and in the Core's RK4 now
  differ by less than a millimetre.
- Chaos's state is read and written through the physics-thread handles
  (`FRigidBodyHandle_Internal`) in the callback; mass changes follow the fuel in 1 % steps
  from the game thread (mass only matters to collisions; the universe gives accelerations).
- Attitude is held perfectly: the body's rotation is set every step to the commanded
  direction and its angular velocity zeroed. Reaction wheels and torques come with parts (M5).
- What is drawn is interpolated by Chaos about two steps behind the simulation
  (`p.AsyncInterpolationMultiplier`). `APeriapsisBubble::GetRenderTime` maps Chaos's result
  time to universe time, and bodies, orbits and vessels on rails are drawn at that time.
- Async physics with substepping: `bSubstepping` is ignored while `bTickPhysicsAsync` is on.
  The fixed 120 Hz async step is what runs (open point from ADR 0001).
- Placeholder meshes: a 2 x 6 m cylinder for the ship, a 3 m cube for other vessels.

## Tests

- Core (`LocalFrameTests.cpp`): translations, the origin's orbit, 1,060 km of integration with
  rebases, rebasing, change of body, hysteresis, prediction kept or dropped, impacts and SOI
  changes from physics, and a burn in the frame against the integrated burn.
- Sim (`BubbleTests.cpp`): rails → physics at 1x and 4x → rails, with the velocity jump; a
  burn in the bubble against the same burn on rails (semi-major axis within 1 mm), the
  floating origin and the stage leaving the bubble; validation of `other_vessels`.
- Editor (PIE, real Chaos): coasting keeps both vessels on their orbits (5e-20 m for the ship,
  2e-6 m for the stage after 3 s), and a 32 s burn at 4x with a rebase matches the Core.

## On-screen check (2026-10-16)

In game mode: 155 to 174 FPS in the bubble, HUD showing 2 vessels in the bubble and the stage
at 1.50 km. A Windows dialog unrelated to the project kept mouse input from reaching the game,
so zooming in on the meshes and flying a burn on screen are still to be checked by a person.

## Pending

- The rotating frame near the surface (M4).
- Collisions between vessels work through Chaos but were not tested on screen.
- `-ini:Engine:[SystemSettings]:r.WarnOfBadDrivers=0` skips the AMD driver dialog for local
  test launches; updating the driver is the real fix.
