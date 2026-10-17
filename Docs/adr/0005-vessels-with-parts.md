# ADR 0005 — Vessels with parts (M5)

Date: 2026-10-16. Status: accepted; the builder's look and feel is judged by a person.

## Parts as data

Each part is a JSON file in `Data/parts/` named after its id, close to the schema of spec 7.1:
dry mass, cost, geometry, attach nodes, joint strength, resources and modules (`Engine`,
`Decoupler`, `Command`). `Data/parts/resources.json` lists the resources. Differences from the
spec's sample:

- **Geometry is data.** A part is a frustum along its +Z axis (tanks, cones, adapters, engine
  bells) or a box. The same solid gives the part's mass distribution (its centroid and
  inertia), its collision hull, its drawn mesh and its extent. There are no meshes to author.
- **Propellants are an object**, resource to share of the mass flow
  (`{"fuel": 0.45, "oxidizer": 0.55}`): a list of names cannot express the mixture.
- **Resources are measured in kilograms.** Volumes and densities can come later; the rocket
  equation only needs masses.
- `max_temp_k` and `tech_required` are left out until heating (M6) and the tech tree (M9).

The loader checks every reference (resources, node ids), positive numbers, unit node
directions and that propellant shares add up to 1, and names the file and field of each
problem. 14 parts ship with M5.

## Designs

A design (`Data/vessels/*.json`, or `Saved/Designs/` from the builder) is a tree of parts, root
first, parents before children. A part attaches by one of its nodes or by its side, to a node
or to the side of its parent (at an angle and height); the two points meet with the part
facing back. A roll reference keeps the part's axis along the vessel's whenever it can, so a
booster on the side of a tank stands upright. `symmetry: N` places N copies evenly around the
parent's axis, and anything attached to one copy goes on all of them. Stages fire in order
from 0 (the spec leaves the numbering open; KSP counts down). Saved designs list every copy
with its `symmetry_group`, and reading them back gives the same placements, stages and groups.

## The vessel is one rigid body with a logical structure

Spec 5.2 recommends a compound rigid body plus a logical structural model. That is what flies:

- **Core** (`Periapsis::Core::Vessel`): the parts in flight, the propellant in each tank, the
  lit engines and the next stage. It gives mass, centre of mass and inertia tensor (spec 5.3),
  thrust and mass flow at an ambient pressure (Isp and thrust interpolated, mass flow constant),
  and the extent of the vessel in any direction (ground clearance).
- **Propellant flow** (spec 5.4): parts connected without crossing a decoupler share
  propellant (a decoupler can allow crossfeed). An engine burns from every tank of its domain;
  higher `flow_priority` tanks empty first, tanks of the same priority drain together in
  proportion to what they hold. An engine flames out when any of its propellants is gone from
  its domain.
- **Staging** (spec 5.5): a stage lights its engines and fires its decouplers. A decoupler
  breaks its joint to its parent and leaves with what hangs below it; both sides get the
  ejection impulse, in opposite directions, and the momentum and the centre of mass of the
  whole are conserved. The flight's state moves to the new centre of mass; each piece becomes a
  vessel of its own, with no control, keeping its attitude.
- **Joint loads** (spec 5.2): while the engines push, every joint carries what its subtree
  needs to keep up with the vessel: a force `m a − T` and its moment about the joint, from the
  parts' centres of mass and the engines' thrust lines. Gravity pulls on every part alike and
  adds nothing in flight. A joint over its strength breaks; the piece below flies off. The
  shipped Trident peaks at 75 % (the second-stage engine, which carries the core and the
  boosters below it, just before the boosters run dry).
  Ground contact loads and impacts per part (spec 5.9) are not modelled yet.
- **Attitude** is still held perfectly, as in M3; the roll is fixed by convention (the vessel's
  X axis as close as possible to the body's pole), and every placement in space follows it. A new
  mode, `up` (key `9`), points straight away from the body: the launch attitude. Radial out is
  square to the velocity, as in KSP, and on the pad the velocity is the body's rotation, so a
  rocket launched radial out leaned west by 25 degrees in five seconds and drifted 55 m/s over
  the ground (`Periapsis.Sim.Launch.StraightUpFromThePad` holds it under 3 m/s; it drifts 0.2).

## Delta-v per stage

`AnalyzeStages` flies the stage sequence at full throttle on a copy of the vessel. A stage
burns until every engine the next stage drops has flamed out (boosters drop when they are
spent while the core keeps burning), or until nothing burns. The burn is split at each
flameout and each piece is exact: `dv = F / mdot · ln(m0 / m1)` with the effective Isp of the
engines burning in it. It reports delta-v, burn time, start and end mass and thrust-to-weight,
in vacuum and at sea level. The HUD shows what is left; the builder shows the whole sequence.

The flight burns the same way: thrust steps end at the next flameout, so the mass flow is
constant within a step and the integration (RK4 on rails, the rocket equation per physics step
in the bubble) has nothing to smooth over.

## Acceptance (spec 14): a three-stage rocket of 50+ parts

`Data/vessels/trident.json`: a pod, an upper stage, a second stage, and a core with eight
boosters, 54 parts, 47.8 t, in four staging events (core and boosters, boosters off, second
stage, upper stage).

- Worked out by hand from the parts' numbers, stage by stage, in vacuum and at sea level, the
  analysis matches to 1e-9 (criterion: 1 %). Vacuum: 2165, 1489, 1722 and 2502 m/s, 7878 m/s
  in all; thrust-to-weight on home at ignition 3.44, 3.87, 3.31 and 2.13.
- Flown through its stages in nearly empty space, on rails and in the physics bubble, it
  delivers 2164.7, 1489.3, 1722.3 and 2502.0 m/s (within 0.02 %), and its acceleration at each
  ignition gives the same thrust-to-weight within 0.01 %. Eight boosters, the core and the
  second stage come off as eleven vessels.

## Chaos

Each vessel is one `UProceduralMeshComponent`: a section per part category, coloured, and a
convex hull per part. Its origin is the vessel frame's origin, and the universe's states are
those of the centre of mass, so the bubble converts between the two and turns the body around
its centre of mass. The centre of mass moves through the structure as the tanks drain; the
structure stays put. A vessel that loses parts gets a new body, rebuilt from its flight's state
on the next step, and the pieces join the bubble as vessels of their own. Staging asked for
between physics steps waits for the next one: Chaos is only touched from the physics thread.

In Play-In-Editor the Trident stands on the pad on its 54 hulls, falls asleep, lifts off
200 m in 4 s, and drops its eight boosters, which fall away as bodies of their own.

## The builder

Spec 8.2, keyboard first (no assets, no Slate): `-Builder` on the command line or `B` in flight
opens it. The design stands at the world origin with the centre of mass marked, the target (a
free node, or a side at an angle and height) marked with an arrow, and the part about to go
there drawn in green with its symmetry copies. Parts are attached, removed with what hangs
below them, undone, moved between stages; stages follow the automatic rule (the bottom fires
first, an engine lights with the decouplers that uncover it) until one is set by hand. Stats
per stage, in vacuum and at sea level, against home's gravity, with mass and cost, update on
every change, with the design's problems (no command pod, an engine in no stage). `F5` saves
to `Saved/Designs/`, `O` cycles through the shipped and saved designs, `L` launches from the
pad (`scenarios/launch_pad.json`).

Mouse placement, mirror symmetry, centre of pressure (needs M6) and subassemblies are left for
later; so are reaction wheels, gimbals and manual attitude control.

## QA pass

Before handing M5 over, the builder and a launch were played on screen and the builder was
fuzzed (`Periapsis.Sim.Parts.BuilderFuzz`: 60 sessions of 250 random key presses, from empty,
from a pod and from the Trident; after every press the design is valid, weighs, analyses and
survives a save and a load, and each final design flies its stages on rails and in a bubble
with every state finite). What the screen showed and was fixed:

- The camera orbited about the world's Z, which on the pad at latitude 5 is nearly horizontal:
  the ground looked like a wall and the rocket lay on its side. Near the ground the camera's up
  is now the local vertical, fading back to the world's above twice the rotating-frame altitude.
- The camera trailed a fast rocket by a frame (the view ticks after the camera manager, to have
  the physics results); it takes the view again after moving.
- F1 (help) and F5 (save) also fired the engine's debug view modes (wireframe, shader
  complexity); those bindings, and the Shift ones, are removed in `DefaultInput.ini`.
- Motion blur smeared the rocket the camera follows; it is off.
- The builder's fill light competed with its main light for forward shading (an on-screen
  engine warning over the parts list); the main light has the priority.
- The builder framed the centre of mass at a fixed 60 m: tall rockets were cut and small ones
  tiny. It frames the design's middle and refits whenever the design changes size.
- The first side target (0 degrees) was behind the rocket; the builder camera now faces it, and
  the target arrow draws over the parts.
- HUD text had no shadow (unreadable over the bright ground) and its columns were padded with
  spaces in a proportional font; cells are tab-separated and aligned by measured width.
- Parts were one flat colour per category, so tanks, pods and engines read alike. Each part is now
  drawn from a look built in the Core (`BuildPartLook`) inside its colliding solid: dark rings at
  the joints of tanks and adapters, a housing, neck and open bell on engines (rust inside), a
  window band and heat shield on pods, a seam on decouplers, a red tip on noses.
- The ground was the body's one display colour, light blue on home. Terrain profiles now list
  `ground_colors` (stops by height, blended, and a steep-slope colour); the terrain paints them
  per vertex, with the engine's lit vertex-colour material.
- The scene was overexposed (white parts clipped, the ground washed out): the sun went from 6 to
  3, and a faint shadowless light from the camera lights the side away from the sun, since there
  is no sky yet.
- The camera's up follows the local vertical while the camera is close to the vessel (within 2 %
  of the body's radius, fading out at 4 %), at any altitude; zoomed out to the orbits it is the
  world's again.

## Pending

- Ground contact loads, impact speed per part and landing legs (spec 5.9, 5.10).
- Torques: reaction wheels, gimbal, off-axis thrust (only the axial part of each engine's
  thrust counts now).
- Debris lifetimes: spent stages stay as vessels until they crash or leave the bubble.
- The builder's mouse interaction, real materials and a launch pad are for a person.
