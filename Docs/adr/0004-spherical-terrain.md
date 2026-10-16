# ADR 0004 — Spherical terrain and landing (M4)

Date: 2026-10-16. Status: accepted; the visual check (no obvious popping on a descent) is done
by a person.

## Terrain as a function

The relief of a body is a pure function of the direction, in the body-fixed frame
(`Periapsis::Core::TerrainShape`): sums of octaves of improved gradient noise, in `fbm` layers
(-A to +A) and `ridged` layers (0 to +A), described in `Data/terrain/*.json` and referenced
from each body (`"terrain": {"seed", "profile"}`). The noise shuffles its permutation with
SplitMix64 written out in the Core, so the same seed gives the same ground on every compiler;
CI pins values on Windows and Linux. The shape knows strict bounds of its height and of its
slope, which the prediction uses (below).

Rendering, collision and the analytic ground queries all evaluate this one function, so they
agree by construction. There are no hand-painted heightmaps yet (spec 6.3); they would be one
more layer kind.

## Bodies turn

`rotation_period_s` (and `rotation_at_epoch_deg`) now matter: bodies turn around +Z, with the
angle reduced modulo the period so it keeps its precision. Terrain, landed vessels and the
physics frame near the ground live in the body-fixed frame. The sunlight comes from a fixed
inertial direction (there is no star in the data yet), so the ground goes through day and
night.

## The physics frame near the ground turns with the body

ADR 0003 left open the spec's recommendation (3.4): near the surface, simulate in a frame
that turns with the body. `LocalFrame` now has two kinds:

- **Free-falling** (as in M3), in space.
- **Body-fixed**, below `rotating_frame_altitude_m` (per body; 20 km on home, 10 km on the
  moon; leaving it at 1.25 times that). The origin is a point fixed to the ground and the axes
  are east, north and up there. Gravity comes with the centrifugal and Coriolis terms, held at
  mid-step like the free-fall one (a 40 s, 400 m/s descent integrated this way ends 7e-7 m
  from Kepler). A rebase only moves the origin; its velocity is the ground's.

The ground stands still in this frame, which is what lets a landed vessel be truly at rest and
the collision mesh be static. Switching kinds re-expresses every simulated body exactly.

## Cube-sphere with error-driven detail

Each body is a quadtree on the six faces of a cube, projected with the "spherified cube"
mapping. Chunks have 33 x 33 vertices and a skirt that hangs below the ground, so seams
between levels never show the sky (`BuildTerrainChunk`).

Detail is chosen by **geometric error**, not by distance alone: each chunk measures how far its
mesh strays from the true ground at its cell centres, and splits while that error, seen from
the camera, is above 1.5 pixels (`SelectTerrainNodes`). This is the "no obvious popping"
criterion of M4 written down: a change of detail never moves the ground on screen by more
than 1.5 pixels. It also keeps the chunk count low: 129 to 270 chunks with the camera on the
ground, down to 6 m spacing where the relief needs it. A first version that split by vertex
spacing drew 10,000 chunks.

There is no geomorphing: without project materials (CLAUDE.md: no binary assets) the vertex
shader cannot blend levels. The error bound is what keeps transitions small.

Chunks are generated on worker threads (`UE::Tasks`, at most 24 in flight, about 3 ms each)
and cached (4,000 per body, least recently used out). A node whose children are not all ready
is drawn instead of them, so there are never holes. They render with
`UProceduralMeshComponent` and the engine's basic material tinted per body: a terrain material
(slope and altitude blending, detail textures) is for a person to author.

## Collision and contact

- **Collision:** in the body-fixed frame and within 300 m of the ground, a 33 x 33 grid at 3 m
  spacing (96 m square) on the analytic ground, centred under the active vessel and rebuilt
  when it strays 12 m from the centre, after a rebase, or when the frame changes. It is cooked
  synchronously: at this size it costs a few milliseconds, and it is only rebuilt while the
  vessel moves over the ground. The spec asks for asynchronous cooking; that is left until the
  patch has to grow.
- **Analytic ground** (the spec's fallback): the universe checks every simulated vessel against
  the terrain function. A vessel below the ground is put back on it, and when the physics scene
  has no ground (the point-mass scene in tests, or Chaos before the patch exists) the universe
  holds it up and stops it from sliding.
- **Impacts:** touching the ground faster than 12 m/s destroys a vessel (no landing legs before
  M5). On rails, the prediction now ends at the real ground: the impact event is the altitude
  above the terrain, sampled with steps that cannot skip a mountain (bounded by the relief's
  height and slope), 0.65 ms per prediction.
- **Rest (spec 5.10):** a vessel within 0.5 m of the ground, slower than 0.05 m/s relative to
  it, with the engine off, for 1 s, lands: its flight becomes `Landed` (it turns with the body
  at a fixed body-fixed position, costs nothing and can warp at any speed), and its Chaos body
  sleeps. Throttle up and it takes off.
- **Vessels have infinite inertia** in Chaos and their attitude is held by setting it. On the
  ground the attitude is left alone and the free-flight position correction of ADR 0003 is not
  applied: both fought the contact solver, and the ship crept along the ground or tipped over
  and rolled.

## Data

- Ship hull sizes (`length_m`, `diameter_m`, `size_m` for other vessels) and its starting
  `attitude` are data now.
- `start_surface` (body, latitude, longitude, height above the ground, vertical speed) starts a
  scenario near the ground; `scenarios/landing_hop.json` uses it.
- The prototype ship has 200 kN and 9,000 kg of fuel: thrust-to-weight 1.6 on home when full
  and 4,045 m/s, enough to land on home from the 100 km orbit with some margin.
- `-Scenario=<path>` and `-CameraDistance=<m>` on the command line pick the scenario and the
  starting camera distance.

## Tests

- Core: noise determinism (pinned values), range and gradient; terrain bounds and slope; body
  rotation; the body-fixed frame (round trips, felt gravity, a free fall against Kepler);
  landed flights; impacts on the relief; cube-sphere continuity, chunk orientation and seams;
  detail selection (the sphere tiled once, every chunk within the pixel error); the collision
  patch on the analytic ground.
- Sim: a descent from orbit flown through the controls (deorbit, coast at 100x, brake against
  the ground speed) that lands, warps at 1000x landed, comes back to the bubble asleep and
  takes off; a hard impact that crashes.
- Editor (PIE, real Chaos): the ship drops on the collision patch, lands 2.6 cm from the
  analytic ground, sleeps, and does not move by a millimetre in 3 s; the terrain is drawn
  with about 260 chunks down to level 14.

## On-screen check (2026-10-16)

In game mode with `-Scenario=scenarios/landing_hop.json -CameraDistance=40`: the ship stands
on the terrain and casts its shadow on it, 270 chunks down to level 14, 115 FPS. From orbit
the planet shows its relief at 131 FPS. A Windows dialog unrelated to the project blocked mouse
input again, so a descent from orbit watched for popping is still to be done by a person.

## Pending

- Terrain material, oceans (altitude 0), and hand-painted heightmaps.
- Asynchronous cooking of a larger collision patch.
- Scaled space for distant bodies (spec 6.5): far bodies are coarse chunks, which works at
  these sizes.
- The HUD's apoapsis and periapsis are meaningless while landed.
