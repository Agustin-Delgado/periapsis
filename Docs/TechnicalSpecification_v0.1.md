# Technical Specification

**Space simulator for rocket building and orbital exploration**

Unreal Engine 5 · C++ · Windows

| | |
|---|---|
| Document | Technical specification (reference for starting development) |
| Version | 0.1 – draft |
| Date | October 14, 2026 |
| Project | Name pending (in this document: PROJECT) |
| Status | Before writing code: open decisions listed in section 16 |

This document gathers the decisions on engine, architecture, physics, data, gameplay, progression, graphics, milestones and risks discussed so far, plus new recommendations to fill the gaps. Figures marked "to be validated" are initial estimates that must be confirmed with prototypes.

## Contents

- 1. Executive summary
  - 1.1 Design pillars
  - 1.2 Scope
  - 1.3 Engineering principles
  - 1.4 Intellectual property
- 2. Stack and technical decisions
- 3. General architecture
  - 3.1 Layers and dependencies
  - 3.2 UE modules
  - 3.3 Two simulation regimes
  - 3.4 Units and reference frames
  - 3.5 Floating origin
  - 3.6 Time
- 4. Orbital mechanics (Core module)
  - 4.1 Representation
  - 4.2 On-rails propagation
  - 4.3 Spheres of influence and predicted trajectory
  - 4.4 Maneuvers
  - 4.5 Delta-v per stage
  - 4.6 Numerical integration
  - 4.7 Advanced planning (after the MVP)
  - 4.8 N-body as an optional improvement
  - 4.9 Accuracy requirements and tests
- 5. Vessel simulation
  - 5.1 Data model
  - 5.2 Physical representation (key decision)
  - 5.3 Mass and inertia
  - 5.4 Propulsion and resources
  - 5.5 Staging and decoupling
  - 5.6 Attitude control
  - 5.7 Aerodynamics
  - 5.8 Heating
  - 5.9 Collisions and damage
  - 5.10 Landing and rest
- 6. Celestial bodies, terrain and atmosphere
  - 6.1 Body definition
  - 6.2 System scale (design lever)
  - 6.3 Spherical terrain
  - 6.4 Atmosphere, sky and oceans
  - 6.5 Rendering distant bodies
- 7. Data and content
  - 7.1 Part schema
  - 7.2 Data rules
  - 7.3 Minimum content for v1.0 (proposal)
- 8. Gameplay and user experience
  - 8.1 Scenes
  - 8.2 Builder
  - 8.3 Flight
  - 8.4 Orbital map
  - 8.5 Accessibility and localization
- 9. Progression, campaign and story
  - 9.1 Game modes
  - 9.2 Progression systems
  - 9.3 Story
  - 9.4 Event and mission system
- 10. Graphics and audio
  - 10.1 Performance budgets (to be validated)
- 11. Performance and scalability
- 12. Repository and workflow with Claude Code
  - 12.1 Folder structure
  - 12.2 Recommended contents of CLAUDE.md
  - 12.3 Reference commands (adjust paths and version)
  - 12.4 Working with an agent that cannot see the viewport
  - 12.5 Git and CI
- 13. Environment setup (checklist)
- 14. Milestones and acceptance criteria
- 15. Risks and mitigations
- 16. Open decisions and first steps
  - 16.1 Open decisions
  - 16.2 First day of work
- Appendix A. Reference formulas
- Appendix B. Glossary
- Appendix C. Reading and documentation to consult

## 1. Executive summary

**Goal.** Build a 3D space simulator for rocket building and orbital exploration on Unreal Engine 5, within the genre popularized by Kerbal Space Program (KSP): assemble vessels from parts, launch them, perform orbital maneuvers and explore a planetary system. The project will have its own universe, art, names and narrative, and aims to improve on the genre on four fronts: physics, graphics, story and progression.

### 1.1 Design pillars

- **Believable, stable physics.** Accurate orbital mechanics, vessels without the classic "Kraken" (explosions or shaking caused by unstable joints), more realistic aerodynamics and heating.
- **Free-form building with useful information.** Node-based builder with delta-v, TWR and stability computed live.
- **Progression with decisions that matter.** Research, budget, reputation, personnel and risk of failure.
- **Emergent story.** A campaign with milestones, discoveries and a rival agency that reacts to the player's progress.
- **High-end visual presentation.** Scattering atmospheres, volumetric clouds, global illumination, and engine and reentry effects.

### 1.2 Scope

**In v1.0**

- Own planetary system (star, home planet, moon, at least 3 to 5 more bodies)
- Part-based rocket builder, staging, symmetry
- Flight with physics, atmosphere, reentry, landing
- Orbital map with maneuver nodes and trajectory prediction
- Campaign with tech tree, budget, contracts, personnel and story
- Sandbox mode with everything unlocked

**Out of v1.0 (design so it can be added, do not implement it)**

- Multiplayer
- VR
- Mod and workshop support (keep data open so it can be enabled later)
- Colonies, bases and industrial production
- Consoles
- Vehicles with flexible/soft parts (cables, ropes)

### 1.3 Engineering principles

- **C++ first.** Blueprints only for thin glue (UI, materials). Blueprints and other binary UE assets cannot be edited well by an agent or reviewed in Git.
- **Data-driven.** Parts, celestial bodies, technology and missions live in versioned JSON/CSV files.
- **Core testable without the editor.** The orbital math is pure C++ with tests that run in seconds, including in CI.
- **SI and double precision.** Meters, kilograms, seconds, double. Conversion to UE units (centimeters) happens only at the boundary with the engine.
- **Playable milestones.** Every milestone ends in something that can be tried and shown.

### 1.4 Intellectual property

The mechanics of a genre are not protected, but KSP's names, characters, art, audio, code and assets are. The project must use original names, species, characters, parts, interface and soundtrack. Before commercializing, it is advisable to validate this with a legal professional (this document is not legal advice). In addition, every third-party asset (Fab/Marketplace, audio, fonts) must have a license compatible with commercial use.

## 2. Stack and technical decisions

Outcome of the Godot / Unity / Unreal comparison: Unreal Engine 5 is chosen for its graphics ceiling (Lumen, Nanite, Sky Atmosphere, volumetric clouds) and for its double-precision world coordinates. The accepted cost is working almost entirely in C++ and accepting heavier projects.

| Decision | Choice | Reason / note |
|---|---|---|
| Engine | Unreal Engine 5.x (latest stable version at the start; pin the version in the .uproject) | Do not upgrade versions in the middle of a milestone. |
| Language | C++ (Blueprints only for UI and materials) | Editable by an agent, reviewable in Git. |
| Rigid body physics | Chaos (included in UE) | One compound body per vessel, see section 5. |
| Orbital mechanics | Own solver in C++ (Core module) | Engine-independent, double precision. |
| World scale | Large World Coordinates (double precision) + origin rebasing | Do not assume double precision throughout the physics chain; verify in M3. |
| Data | JSON/CSV in Git, imported into DataTables or loaded at runtime | Easy review and diffs. |
| Planetary terrain | Cube-sphere with quadtree LOD, mesh generated at runtime | Nanite does not work for runtime-generated meshes. |
| Atmosphere and sky | Sky Atmosphere + Volumetric Clouds | Only one active Sky Atmosphere (verify), section 6. |
| Effects | Niagara | Engine plumes, reentry, separations. |
| UI | UMG/Slate driven from C++ (CommonUI optional) | Localization with FText. |
| Input | Enhanced Input | Keyboard, mouse and gamepad. |
| Audio | MetaSounds | Sound dependent on atmospheric density. |
| Initial platform | Windows PC | Other platforms later. |
| Version control | Git + Git LFS | Binaries (.uasset, .umap, textures, meshes) through LFS. |
| Tests | doctest or Catch2 (Core, outside the editor) + UE Automation Framework (integration) | The Core is tested in CI without installing UE. |
| Build | Visual Studio 2022 (or Rider) + Unreal Build Tool | Live Coding for iteration. |
| Coding agent | Claude Code on native Windows | Unreal runs on Windows; WSL2 only for auxiliary tooling. |

## 3. General architecture

### 3.1 Layers and dependencies

```
+---------------------------------------------------------------+
| PRESENTATION  UI (UMG), cameras, VFX (Niagara), audio         |
+---------------------------------------------------------------+
| GAMEPLAY      Builder, flight, map, campaign, contracts       |
+---------------------------------------------------------------+
| SIMULATION    Universe, Vessel, local physics, resources,     |
|               aerodynamics, heat, structure                   |
+---------------------------------------------------------------+
| CORE (pure C++, no UE headers)  orbits, integrators,          |
|               units, math utilities                           |
+---------------------------------------------------------------+
```

Rule: each layer depends only on the layers below it.

### 3.2 UE modules

| Module | Contents | Dependencies |
|---|---|---|
| PROJECTCore | Orbital math, Kepler, SOI, maneuvers, integrators, unit conversion | None (standard C++) |
| PROJECTSim | Universe state, bodies, vessels (Vessel), parts, resources, time, saving | Core |
| PROJECTGame | Actors, controllers, UI, cameras, terrain, VFX, campaign | Core, Sim, Engine |
| PROJECTEditor | Editor tools, data validators, importers | Game, UnrealEd |
| Tools/CoreTests | CMake project with the Core tests, runs outside UE and in CI | Core |

### 3.3 Two simulation regimes

As in KSP, not everything is simulated with physics. Distant vessels are propagated with orbital equations ("on rails") and only those near the active vessel enter the "physics bubble".

| | On rails | Physics bubble |
|---|---|---|
| Method | Analytical Kepler solution, no integration | Chaos + own forces (gravity, thrust, aerodynamics) |
| When | Distant vessels, vessels without thrust at a large distance, planets and moons | Active vessel and those within the bubble radius (initial: 2.5 km on entry, 2.2 km on exit; to be validated) |
| Time warp | Up to 100,000x | Up to 4x (with sub-steps) |
| Cost | Negligible, thousands of vessels | High, few vessels |
| Persisted state | Keplerian elements and epoch | Position/velocity in the local frame |

The transition converts position and velocity to Keplerian elements (on exit) or evaluates them to obtain a Cartesian state (on entry). Hysteresis is added to avoid repeatedly entering and leaving. A vessel on the ground is persisted with latitude, longitude, altitude and orientation, and is "frozen" when Chaos puts it to sleep.

### 3.4 Units and reference frames

- **Simulation units:** meters, kilograms, seconds, newtons, double. UE uses centimeters: the conversion lives in a single boundary class (for example `FUnitBridge`) and units are never mixed in the rest of the code.
- **Frames:** (a) universal/inertial root; (b) inertial, centered on a body; (c) body-fixed (rotates with the planet); (d) local physics frame, with its origin at the active vessel.
- **Open decision with a recommendation:** inside the bubble and below a "rotating-frame altitude" (per body), simulate in the body-fixed frame applying fictitious forces (centrifugal and Coriolis); above it, in an inertial frame. This simplifies landing. Validate in M3.

### 3.5 Floating origin

The active vessel is kept close to the physics origin. When it moves farther than a threshold (initial: 5 km, configurable), the whole local world is shifted to re-center it. UE offers World Origin Rebasing and Large World Coordinates on the rendering side, but double precision must not be assumed throughout the physics chain: the rule is to keep the Chaos scene always close to the origin. Verify in the M3 prototype.

### 3.6 Time

- Universal time UT in double seconds, the single source of truth for orbits, campaign events and saving.
- Fixed physics steps (60 to 120 Hz) with Chaos async physics and sub-stepping enabled; simulation logic uses fixed steps so it is reproducible.
- On-rails warp levels: 1x, 5x, 10x, 50x, 100x, 1,000x, 10,000x, 100,000x (to be validated). Warp is automatically limited when entering an atmosphere or near a body (depending on altitude).

## 4. Orbital mechanics (Core module)

### 4.1 Representation

The source of truth for a vessel is the state vector (position and velocity relative to the dominant body, with an epoch). Keplerian elements (semi-major axis a, eccentricity e, inclination i, longitude of the ascending node, argument of periapsis, mean anomaly and epoch) are derived when needed (drawing the orbit, showing Ap/Pe). This avoids the singularities of the classical elements in circular (e = 0) or equatorial (i = 0) orbits.

### 4.2 On-rails propagation

- Solve Kepler's equation with Newton-Raphson using a good initial guess and a bisection fallback; use the hyperbolic form for e greater than 1 and handle the near-parabolic case.
- Consider the universal-variable formulation to handle elliptic, parabolic and hyperbolic orbits with the same code.
- Planets and moons follow fixed Keplerian orbits (system ephemerides defined in data).

### 4.3 Spheres of influence and predicted trajectory

- Patched conics approximation: each body has a sphere of influence (initial radius from the Laplace formula, with the option of a manual value in data).
- Prediction: chain conic segments up to N patches (initially 5), detecting sphere entries and exits by root finding along the trajectory. The state is transformed between frames when crossing the boundary.
- The prediction is computed on a separate thread and is invalidated when thrust or a maneuver node changes.

### 4.4 Maneuvers

- A maneuver node is (UT, delta-v prograde, normal, radial) in the local orbital frame.
- The resulting orbit is recomputed instantly and drawn on the map.
- Estimated burn time from the rocket equation and the stage data; the burn is centered on the node (half before, half after) with a warning to the player.

### 4.5 Delta-v per stage

The delta-v of each stage is computed with the rocket equation, dv = Isp · g0 · ln(m0 / m1), taking into account vacuum or ambient-pressure Isp, the resource consumption order and fuel flow between tanks. This feeds the builder and the HUD.

### 4.6 Numerical integration

- When there is thrust or atmospheric drag, the vessel leaves the rails and is integrated numerically (Velocity Verlet or RK4 with sub-steps; optionally an adaptive step such as Dormand-Prince).
- When the perturbation ends, the state is converted to elements and goes back on rails.
- Gravity is evaluated in double precision and separately from Chaos, which receives the resulting force.

### 4.7 Advanced planning (after the MVP)

- Lambert solver for transfers between bodies and porkchop plots.
- Launch window and transfer window assistant, with maneuver suggestions.

### 4.8 N-body as an optional improvement

Proposed improvement over KSP: a high-fidelity mode in which the vessel's predicted trajectory includes the gravity of several nearby bodies (planets stay on rails and are used as ephemerides). Risks: loss of stability at high warp and energy drift. That is why it is not applied to the planets, only to the vessel, and it is released as an option after the MVP (see open decision 5).

### 4.9 Accuracy requirements and tests

- State-to-elements conversion and back: relative error below 1e-9 across the case library.
- Relative drift of energy and angular momentum below 1e-9 after 1,000 orbits on rails (close to machine precision).
- Reference cases: low orbit, geostationary transfer, transfer to the moon, hyperbolic flyby, near-parabolic, retrograde, circular, equatorial.
- Property tests with random orbits and comparison against analytical solutions.
- These tests run in CI without UE (Tools/CoreTests).

## 5. Vessel simulation

### 5.1 Data model

A Vessel is a graph of parts (rooted tree, symmetries and optional struts). Each part declares dry mass, attach nodes, collision shapes, resources and a list of modules (engine, tank, decoupler, parachute, reaction wheel, solar panel, battery, experiment, landing gear, etc.).

### 5.2 Physical representation (key decision)

Recommendation: each vessel is a single compound rigid body in Chaos (multiple collision shapes, aggregated mass and inertia) plus a logical structural model. Each joint has a strength (maximum force and torque). On every step, the loads on each joint are computed from acceleration, downstream mass and moments. If the limit is exceeded, the joint breaks and the vessel splits into two bodies.

| Approach | Advantages | Disadvantages |
|---|---|---|
| Compound body + logical structure (recommended) | Eliminates the "Kraken" of physical joints, low cost, deterministic, scales to hundreds of parts | No visible flexing; the load calculation has to be written |
| One Chaos joint (constraint) per connection | "Natural" flexing and breaking | Unstable with many parts and disparate masses, expensive, hard to tune |
| Hybrid (future) | Connections marked as flexible (girders, panels, legs) use constraints; the rest, compound | More complexity; do it only if the design calls for it |

### 5.3 Mass and inertia

As fuel is consumed, the mass, center of mass and inertia tensor change. They are recomputed in the own solver and updated in Chaos at a fixed interval or when the change exceeds a threshold.

### 5.4 Propulsion and resources

- Thrust and Isp interpolated between vacuum and sea level according to ambient pressure; mass flow rate mdot = F / (Isp · g0).
- Throttle control, thrust vectoring (gimbal), ignition and reignition, optional ignition failures.
- Resources as data (fuel, oxidizer, monopropellant, electric charge, etc.) with a configurable flow graph: tank priority, crossfeed, tanks in series or in parallel.

### 5.5 Staging and decoupling

- Stage list editable in the builder and during flight.
- The decoupler applies a separation impulse and creates a new vessel; fairings and debris as cheap entities with a lifetime.

### 5.6 Attitude control

- SAS with modes (hold attitude, prograde, retrograde, normal, radial, toward the node).
- PID controller with reaction wheel and RCS torque limits.
- Autopilot to hold an attitude during a burn (optional).

### 5.7 Aerodynamics

- Density per body through a curve or table in data, with the exponential model rho(h) = rho0 · exp(-h / H) as a fallback.
- Drag per part: Fd = 0.5 · rho · v² · Cd · A, with Cd and effective area per face and angle of attack, and with occlusion (covered parts do not contribute).
- Lifting surfaces and center of pressure; the builder shows the center of mass and of pressure to anticipate stability.
- Improvement to evaluate (phase 2): drag derived from a simple voxelization of the silhouette and a Mach-number-dependent curve.

### 5.8 Heating

- Heat flux estimated with a Sutton-Graves-type relation, q = k · sqrt(rho / Rn) · v³, with Rn the effective nose radius.
- Temperature per part with heat capacity, basic conduction between connected parts and dissipation.
- Heat shields and ablative shields as modules; if a part exceeds its maximum temperature, it fails or explodes.
- Plasma visual effect tied to the heat flux (Niagara).

### 5.9 Collisions and damage

Each part has impact-speed and load thresholds; when they are exceeded it is destroyed or comes off. The results are recorded for the player's telemetry.

### 5.10 Landing and rest

- Legs with suspension (spring-damper) and friction configurable per terrain material.
- Rest detection: when the criteria are met for a period of time, the vessel goes to sleep and switches to a cheap "landed" state, avoiding indefinite shaking.

## 6. Celestial bodies, terrain and atmosphere

### 6.1 Body definition

All bodies are described in data. Example (illustrative values, to be adjusted):

```json
{
  "id": "home_planet",
  "parent": "star",
  "radius_m": 1000000,
  "mu_m3_s2": 9.81e12,
  "rotation_period_s": 21600,
  "orbit": { "a_m": 1.0e11, "e": 0.0, "i_deg": 0, "lan_deg": 0, "argp_deg": 0, "m0_deg": 0, "epoch_s": 0 },
  "atmosphere": { "height_m": 70000, "scale_height_m": 5600, "density_curve": "curves/home_density.csv", "sky": "sky/home_sky.json" },
  "terrain": { "seed": 1337, "sea_level_m": 0, "profile": "terrain/home.json" },
  "soi_m": null
}
```

### 6.2 System scale (design lever)

A real-scale system makes ascents long and tedious. A reduced home planet, adjustable through data, is proposed. Suggested initial value (to be validated): radius 1,000 km and surface gravity 9.81 m/s², which gives a circular velocity at sea level of sqrt(g · R), approximately 3.13 km/s, and a delta-v to low orbit of around 4.0 to 4.3 km/s including losses. It is tuned in the M2 prototype until the ascent feels right.

### 6.3 Spherical terrain

- **Structure:** cube-sphere (6 faces) with a quadtree per face. Each node is a mesh chunk (for example 33×33 or 65×65 vertices). LOD by screen-space error or distance, with skirts or morphing (reference: CDLOD).
- **Generation:** heights from noise (fBm, ridged, domain-warped) plus hand-painted heightmaps for special sites. Generated on worker threads and cached.
- **Rendering:** mesh generated at runtime (Nanite does not apply). ProceduralMeshComponent is enough for the prototype; evaluate a more efficient real-time mesh component for production.
- **Collision:** only within a radius around the vessels in the bubble, with a higher-resolution collision mesh cooked asynchronously. Fallback: analytical height query (terrain function) for low-level contact and to avoid depending on cooking.
- **Materials:** blending by slope, altitude and biome; triplanar and close-up detail; Runtime Virtual Texturing if needed.

### 6.4 Atmosphere, sky and oceans

- Sky Atmosphere with per-body parameters taken from data. UE supports only one active Sky Atmosphere per level (verify in the chosen version): it is reconfigured when the active body changes and the others are shown with impostors.
- Volumetric Clouds and atmospheric fog; smooth transition from sky to space.
- Oceans: a sphere with its own shader (waves through normal maps, foam, depth); collision as altitude 0 at first. UE's Water system is not designed for planetary scale.
- Stars and Sun: HDR star map, directional light with realistic intensity, shadows and eclipses between bodies.

### 6.5 Rendering distant bodies

Distant bodies are drawn as spheres or impostors with preserved angular size and limited distance ("scaled space"), to avoid depth buffer precision problems. When a body becomes the active one, it is replaced by the full terrain and atmosphere.

## 7. Data and content

### 7.1 Part schema

```json
{
  "id": "engine_small_01",
  "category": "engine",
  "dry_mass_kg": 450,
  "cost": 1200,
  "tech_required": "propulsion_basic",
  "attach_nodes": [
    { "id": "top", "pos": [0, 0, 0.6], "dir": [0, 0, 1], "size": 1 },
    { "id": "bottom", "pos": [0, 0, -0.6], "dir": [0, 0, -1], "size": 1 }
  ],
  "joint_strength": { "force_n": 60000, "torque_nm": 30000 },
  "max_temp_k": 2000,
  "modules": [
    { "type": "Engine", "thrust_vac_n": 60000, "isp_vac_s": 310, "isp_sl_s": 265, "propellants": ["fuel", "oxidizer"], "gimbal_deg": 2 }
  ]
}
```

### 7.2 Data rules

- Stable, unique IDs (never reused); references between data are by ID.
- Validation with JSON Schema and a test that loads all content and checks cross-references.
- Hot reload in the editor to tune parts and balance without restarting.
- Versioned saves with explicit migrations between versions.
- Texts by localization key (es/en from the start), not strings embedded in code.

### 7.3 Minimum content for v1.0 (proposal)

| Category | Initial count | Note |
|---|---|---|
| Celestial bodies | 6 to 8 | Star, home planet, moon, 3 to 5 more bodies of different types |
| Parts | 80 to 120 | Capsules, tanks, engines, decouplers, structure, aerodynamics, power, science, landing |
| Tech nodes | 30 to 40 | With 3 to 5 parts per node |
| Experiments | 15 to 25 | Per biome and situation |
| Template contracts | 20 to 30 | Plus handmade narrative contracts |

## 8. Gameplay and user experience

### 8.1 Scenes

Control center (hub), Builder, Flight, Orbital map (overlaid on flight), Tracking station (vessels, contracts and events) and menus.

### 8.2 Builder

- Node-based assembly with radial and mirror symmetry; alignment aids and stepped rotation.
- Improvement over KSP: delta-v, TWR, mass and cost per stage in real time, with vacuum and sea-level values; visible center of mass and center of pressure; design warnings (unstable, no control, no power).
- Saving and loading designs (JSON), reusable subassemblies, cost and risk estimate.

### 8.3 Flight

- HUD with navball, altitude, speed, TWR, resources, current stage and warnings.
- Cameras: chase, free and pilot (future).
- Quicksave and quickload; revert to launch or to the hangar (depending on the mode's rules).

### 8.4 Orbital map

- Orbits, apoapsis and periapsis, encounter markers, SOI transitions, editable maneuver nodes.
- Time warp control with automatic limits and safety warnings.

### 8.5 Accessibility and localization

- Full control remapping; gamepad support; UI scale and high-contrast mode.
- es/en localization from day one (FText, string tables).

## 9. Progression, campaign and story

### 9.1 Game modes

- **Sandbox:** everything unlocked, no budget.
- **Campaign:** progression, economy, contracts and story.
- **Science (optional):** tech tree and science, without economy or story.

### 9.2 Progression systems

| System | Description | Improvement over KSP |
|---|---|---|
| Tech tree | Nodes that cost research points and have prerequisites; each node unlocks parts and capabilities | Branches with trade-offs (specializing), not just buying everything |
| Science | Experiments per biome and situation, with diminishing returns and transmission or recovery | Data that also feeds story discoveries |
| Budget | Funds from contracts and achievements; cost of parts and of launches; part recovery depending on distance to the launch center | Annual budget and funding with conditions |
| Reputation | Rises with achievements and fulfillment; drops with failures and unfulfilled contracts | Affects available contracts and offers |
| Personnel | Crew members with experience, training and real risk of loss | Careers and risk decisions with narrative consequences |
| Facilities | Upgradable buildings that raise limits on mass, parts and features | Upgrades with construction time |
| Reliability | Each part gains reliability with use; new ones may fail more often | Real risk decisions (test before committing) |

### 9.3 Story

KSP has almost no story: this is a clear differentiator. The proposal is an emergent narrative tied to technical progress, without long cutscenes, with four ingredients:

- **Acts tied to gameplay milestones** (I: suborbital and orbit; II: the moon; III: the inner system; IV: the outer system and the ending). The titles and tone are provisional.
- **Discoveries through exploration:** signals, structures and messages that appear when reaching specific places or gathering a certain amount of science.
- **Rival agency** with a timeline that reacts to the player's progress (races, diplomatic sabotage, collaborations).
- **Narrative contracts** with branches and consequences on reputation, funding and technology.

Pending deliverable: a separate "narrative bible" (setting, species, tone, event chronology, characters). Until it is defined, the event system is implemented without final content.

### 9.4 Event and mission system

Data-driven event machine: conditions (vessel in orbit around a body, accumulated science, unlocked technology, universal time, minimum reputation) that trigger actions (unlock technology, create a contract, show dialogue, change the rival agency's state, store a story flag). It makes it easy to iterate on the narrative without touching C++.

## 10. Graphics and audio

- **Lighting:** Lumen for global illumination and reflections; Virtual Shadow Maps; auto exposure with limits designed for space; sunlight and shadows from bodies.
- **Atmosphere and clouds:** Sky Atmosphere and Volumetric Clouds; a carefully handled space-atmosphere transition.
- **Parts:** static meshes with PBR and Nanite where applicable; materials with wear and temperature-driven emissive glow.
- **VFX (Niagara):** engine plumes that change shape with ambient pressure, reentry plasma, separation sparks, dust on landing.
- **Post-processing:** bloom, lens flare, HDR and color grading consistent with the project's own art.
- **Audio:** MetaSounds; engine and wind sound depends on atmospheric density and on the camera; dynamic music tied to campaign events.

### 10.1 Performance budgets (to be validated)

| Scenario | Target |
|---|---|
| Flight with a vessel of up to 200 parts on a mid-range GPU | 60 FPS at 1440p |
| Visible planetary terrain, sky and clouds | Less than 6 ms of GPU for terrain and atmosphere |
| Simulation and physics | Less than 4 ms of CPU per frame for the active vessel |
| Trajectory prediction | On a separate thread, without blocking the game thread |

## 11. Performance and scalability

- **Physics:** a single physics entity per vessel, simplified collision shapes per part.
- **Part rendering:** Nanite, instancing and static mesh merging; LOD on complex parts.
- **Terrain:** generation and collision cooking on worker threads, with a queue prioritized by proximity.
- **Many vessels:** those on rails only cost one analytical evaluation per query.
- **Profiling from M2:** Unreal Insights, stat unit, stat game; budget review at every milestone.

## 12. Repository and workflow with Claude Code

### 12.1 Folder structure

```
PROJECT/
  CLAUDE.md            project rules for the agent
  PROJECT.uproject
  Config/
  Content/             UE assets (minimal; whatever can goes in Data/)
  Data/                JSON/CSV: bodies/, parts/, tech/, events/, contracts/
  Source/
    PROJECTCore/       pure C++ (orbits, integrators, units)
    PROJECTSim/        universe state, vessels, resources
    PROJECTGame/       actors, UI, terrain, VFX, campaign
    PROJECTEditor/     editor tools
  Tools/
    CoreTests/         CMake + doctest/Catch2 (runs without UE)
    Scripts/           data scripts, validators
  Docs/                specification, decisions (ADR), narrative bible
  .github/workflows/   CI (Core tests)
```

### 12.2 Recommended contents of CLAUDE.md

- Rules: C++ first; do not edit .uasset, .umap or other binaries; data in Data/; SI units and double; conversion only at the boundary; the Core does not include UE headers.
- How to build, test and run (commands below, adjusting paths).
- UE naming conventions (prefixes A, U, F, E, I, T), commit and branch style.
- Definition of "done" per task: builds, tests green, no new warnings, data validated.
- Agent limits: it cannot see the viewport; visual checks are done by the human.

### 12.3 Reference commands (adjust paths and version)

```bat
:: Build the editor (Windows)
"C:\Program Files\Epic Games\UE_5.x\Engine\Build\BatchFiles\Build.bat" ^
  PROJECTEditor Win64 Development -Project="C:\path\PROJECT.uproject" -WaitMutex

:: Core tests (without UE)
cmake -S Tools/CoreTests -B build/coretests
cmake --build build/coretests --config Release
ctest --test-dir build/coretests -C Release --output-on-failure

:: Integration tests with Automation (no rendering)
"C:\...\UnrealEditor-Cmd.exe" "C:\path\PROJECT.uproject" ^
  -ExecCmds="Automation RunTests PROJECT; Quit" -unattended -nopause -NullRHI -log
```

### 12.4 Working with an agent that cannot see the viewport

- Everything measurable becomes a test: orbits, delta-v, conversions, structural loads.
- Debug visualization (lines, text) and clear logs so the human can validate the visuals quickly.
- Automatic screenshots per scenario for human review at every milestone.
- Visual assembly (levels, materials, Niagara) is done by the human; the agent prepares code and data.

Optionally, editor scripting with Python makes it possible to automate asset creation.

### 12.5 Git and CI

- Git LFS for binaries; .gitignore with Binaries, Intermediate, Saved, DerivedDataCache and .vs.
- Minimal CI: build and run the Core tests on every push (does not require installing UE).
- Short branches per milestone and review of every change; ADRs for architectural decisions.

## 13. Environment setup (checklist)

| Step | Detail |
|---|---|
| 1. Hardware | SSD with 150 GB free or more (engine, DDC and project), 32 GB of RAM recommended, GPU with recent DX12 support |
| 2. Unreal Engine | Install the latest stable 5.x version from the Epic Games Launcher; pin the version; source build only if the engine needs to be modified |
| 3. Visual Studio 2022 | "Game development with C++" workload, Windows SDK and the MSVC toolset indicated by the UE documentation for the chosen version |
| 4. Git and Git LFS | Repository with .gitattributes for LFS |
| 5. Claude Code | Installed on native Windows and opened on the project folder; WSL2 is kept for auxiliary tooling |
| 6. Project | New blank C++ project, no starter content, hardware ray tracing disabled at first |
| 7. Settings | Enable sub-stepping and async physics; review Large World Coordinates; set the step size |
| 8. Modules | Create PROJECTCore, PROJECTSim, PROJECTGame and PROJECTEditor; set up Tools/CoreTests |
| 9. CI and CLAUDE.md | Core test workflow and a CLAUDE.md file with the rules from section 12 |

## 14. Milestones and acceptance criteria

Indicative effort for one person working with a coding agent: S days, M 1 to 2 weeks, L 3 to 6 weeks, XL more than 2 months. These are initial estimates, to be revised when M2 closes.

| Milestone | Deliverable | Acceptance criteria | Effort |
|---|---|---|---|
| M0 Foundations | Repo, UE C++ project, modules, CI, CLAUDE.md | Builds in Win64 Development; ctest green in CI | S |
| M1 Orbital core | Elliptic and hyperbolic Kepler, state/elements, propagation, SOI, nodes, rocket equation | Reference suite green; round-trip conversion below 1e-9; energy drift below 1e-9 over 1,000 orbits | M |
| M2 Orbital prototype | Sphere planet, point vessel with thrust, drawn orbit, on-rails warp, basic map | Stable orbit and transfer to a moon with 100,000x warp without visible jumps; 60 FPS | M |
| M3 Physics bubble | Rails-physics transition, floating origin, local frame | Vessel 1,000 km from the origin without visible jitter; velocity discontinuity below 1e-3 m/s at the transition | L |
| M4 Spherical terrain | Cube-sphere with LOD, local collision, landing | Descent from orbit to the surface without obvious popping; 60 FPS; the vessel goes to sleep at rest without shaking | L |
| M5 Vessels with parts | Builder, parts, tanks, engines, stages, logical structure, delta-v | 3-stage rocket with 50 or more parts with delta-v and TWR within 1% of the analytical calculation | XL |
| M6 Atmosphere and reentry | Density, drag, heating, parachutes, Sky Atmosphere | Playable ascent to orbit with a gravity turn; reentry with heating and parachute landing | L |
| M7 Map and planning | Full map, nodes, encounter markers, SOI transitions in the UI | Plan and execute a transfer to the moon using only the UI | M |
| M8 Complete system | 6 to 8 bodies in data, multiple SOIs, scale tuning | Trip to an outer planet with reasonable transfer windows | L |
| M9 Campaign | Tech tree, budget, contracts, personnel, versioned saving | Full campaign playthrough from scratch to the first story milestone, with saving and loading | XL |
| M10 Story and content | Events, dialogues, discoveries, rival agency, final content | Campaign completable from start to finish | XL |
| M11 Polish and launch | Performance, art, audio, localization, QA, Early Access strategy | Performance budgets met; round of external playtests | XL |

## 15. Risks and mitigations

| Risk | Prob. | Impact | Mitigation |
|---|---|---|---|
| Excessive scope (full parity with KSP) | High | High | Work by pillars; freeze scope per milestone; playable demos |
| Numerical precision and jitter | Medium | High | SI and double precision; floating origin; early M3; tests |
| Vessel physics instability (Kraken) | High | High | Compound body and logical structure; sub-stepping; load limits |
| Complex terrain LOD (seams, collision, streaming) | High | Medium-high | Isolated prototype in M4; CDLOD; analytical height fallback |
| Performance with many parts | Medium | Medium | Nanite, instancing, budgets and profiling from M2 |
| Binary assets the agent cannot edit | High | Medium | Data in JSON/CSV; C++ first; editor scripting with Python |
| Build and iteration times in UE | Medium | Low-medium | Live Coding; small modules; Core tested outside the editor |
| Intellectual property | Low | High | Own universe and assets; clear licenses; legal review before release |
| UE version changes | Medium | Medium | Pin the version; upgrade only between milestones and with tests |
| Long project and loss of focus | Medium | High | Small milestones, frequent demos, prioritize the fun of the core |

## 16. Open decisions and first steps

### 16.1 Open decisions

| # | Decision | Current recommendation |
|---|---|---|
| 1 | Name, setting and tone of the universe | Define before M9; does not block M0 to M8 |
| 2 | System scale (home planet radius) | 1,000 km and 9.81 m/s² as a starting point; tune in M2 |
| 3 | Physics reference frame | Rotating near the surface, inertial at altitude; validate in M3 |
| 4 | Vessel structure | Compound body + logical structure; hybrid later if needed |
| 5 | N-body in v1.0? | No; high-fidelity option after the MVP |
| 6 | Platforms and minimum hardware requirements | Windows PC; set a minimum profile after M4 |
| 7 | Business model | Early Access or one-time purchase; affects licensing (5% royalty on the first USD 1 million of gross revenue per product in UE) |
| 8 | Exact UE version | Latest stable 5.x at the start; pin it in the .uproject |
| 9 | Languages | Spanish and English from the start |

### 16.2 First day of work

- Install UE and Visual Studio 2022, create the repository and the blank C++ project.
- Write the CLAUDE.md with the rules from section 12 and place this document in Docs/.
- Open Claude Code in the project folder and ask it for M0 and then M1.

Suggested text for the first request to Claude Code:

> Read CLAUDE.md and Docs/ (technical specification v0.1). Carry out milestone M0: create the modules PROJECTCore, PROJECTSim and PROJECTGame, the Tools/CoreTests project with CMake and doctest, and a CI workflow that runs the Core tests. Then carry out milestone M1: implement in PROJECTCore the Kepler solver (elliptic and hyperbolic), conversion between state vector and elements, propagation, sphere of influence and the rocket equation, with reference tests and the accuracy criteria from section 4.9. Do not edit Unreal binaries. Report which commands you ran and the results.

## Appendix A. Reference formulas

| Name | Formula |
|---|---|
| Rocket equation | `dv = Isp * g0 * ln(m0 / m1)`, `g0 = 9.80665 m/s^2` |
| Mass flow rate | `mdot = F / (Isp * g0)` |
| Vis-viva | `v^2 = mu * (2/r - 1/a)` |
| Circular velocity | `v_c = sqrt(mu / r)` |
| Escape velocity | `v_e = sqrt(2*mu / r)` |
| Period (elliptic) | `T = 2*pi*sqrt(a^3 / mu)` |
| Kepler (e < 1) | `M = E - e*sin(E)`, `M = n*(t - t0)`, `n = sqrt(mu / a^3)` |
| Kepler (e > 1) | `M = e*sinh(H) - H` |
| Sphere of influence | `r_SOI = a * (m / M)^(2/5)` (m: body, M: parent body) |
| Hohmann (r1 -> r2) | `dv1 = sqrt(mu/r1) * (sqrt(2*r2/(r1+r2)) - 1)`<br>`dv2 = sqrt(mu/r2) * (1 - sqrt(2*r1/(r1+r2)))` |
| Drag | `F_d = 0.5 * rho * v^2 * Cd * A` |
| Density (simple) | `rho(h) = rho0 * exp(-h / H)` (H: scale height) |
| Heat flux | `q = k * sqrt(rho / Rn) * v^3` (Sutton-Graves, estimate) |
| Thrust-to-weight ratio | `TWR = F / (m * g_local)` |

## Appendix B. Glossary

| Term | Meaning |
|---|---|
| On-rails | Vessel propagated with orbital equations, without physics |
| Physics bubble | Region around the active vessel where vessels are simulated with Chaos |
| SOI | Gravitational sphere of influence of a body |
| Patched conics | Approximation that joins conic trajectories between spheres of influence |
| Delta-v | Total change in velocity a vessel can achieve |
| TWR | Thrust-to-weight ratio |
| Isp | Specific impulse, engine efficiency |
| Kraken | Simulation failures caused by unstable joints or collisions |
| Floating origin | Technique of re-centering the world to avoid loss of precision |
| LOD | Level of detail that changes with distance or on-screen size |
| LWC | Large World Coordinates, double-precision world coordinates in UE5 |
| ADR | Architecture decision record |

## Appendix C. Reading and documentation to consult

- H. Curtis, *Orbital Mechanics for Engineering Students*.
- R. Bate, D. Mueller, J. White, *Fundamentals of Astrodynamics*.
- D. Vallado, *Fundamentals of Astrodynamics and Applications*.
- F. Strugar, *Continuous Distance-Dependent Level of Detail for Rendering Heightmaps* (CDLOD).
- Official Unreal Engine documentation: Large World Coordinates, Chaos Physics (sub-stepping and async physics), Sky Atmosphere, Volumetric Clouds, Niagara, Enhanced Input, Automation Testing Framework, Live Coding.
- Check editions and versions at the time of use, since the engine documentation changes between versions.
