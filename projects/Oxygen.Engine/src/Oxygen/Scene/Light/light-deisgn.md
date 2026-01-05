
# Oxygen Scene Lighting

This document specifies how Oxygen’s Scene system will represent lights and expose them to the renderer.

It is intentionally written to align with existing Scene architecture and philosophy:

- **Handle/View pattern** for public-facing APIs.
- **No-data policy** on the handle (`SceneNode`); data lives in `SceneNodeImpl` components.
- **Composition-based components** with dependency validation.
- **SafeCall**-style operations: exception-free, resilient to invalid/lazily-invalidated nodes.
- **Typed facades** (like `SceneNode::Transform` / `SceneNode::Renderable`) to keep the `SceneNode` API surface small and stable.
- **Camera-style attachments** where appropriate (lights mirror the Camera `Attach/Detach/Replace/GetAs<T>` API for consistency).

Status: Living design/spec document.

---

## 1. Goals

- Provide a **comprehensive, renderer-friendly lighting representation** attachable to any `SceneNode`.
- Support **typical real-time light types** needed to light a scene:
  - Directional
  - Point
  - Spot
- Ensure lights are **transform-driven** (position/orientation from node transform) and compatible with Oxygen’s coordinate conventions.
- Allow **runtime attach/detach/replace** of light components, consistent with existing component patterns.
- Provide a clear path for **shadows**, **culling**, and **GPU data extraction** without overcoupling Scene to Renderer.

## 2. Non-goals

- No commitment yet to a specific lighting technique (forward+, clustered, deferred, RT, etc.).
- No area/mesh lights, IES profiles, or volumetrics in the first iteration.
- No new scene serialization format design work is required in this document.
  Lights are expected to be persisted using Oxygen’s existing scene/component
  persistence pipeline (PAK / loose-cooked), the same way other node-attached
  components are handled.
- No editor UI spec.

---

## 3. Engine Conventions That Lighting Must Obey

Lighting uses the same immutable engine conventions defined by `oxygen::space`:

- World space: right-handed, Z-up, forward = -Y, right = +X.
- For lights that have a direction (Directional/Spot): **the light’s local forward direction is `oxygen::space::move::Forward`** transformed by the node’s world rotation.

---

## 4. Conceptual Model

### 4.1 Light as a Scene Component

Each light is represented as a component attached to the internal node implementation (`SceneNodeImpl`) via the Composition system.

Key properties:

- Light components derive from `oxygen::Component`.
- Light components should require `oxygen::scene::detail::TransformComponent` as a dependency (same as cameras).
- Policy: **a node can have at most one light component** (one of Directional/Point/Spot).
  - Rationale: matches camera semantics; avoids ambiguous multi-light transforms and simplifies extraction.

### 4.2 Light Type Family

We model light types as separate components (parallel to `PerspectiveCamera` vs `OrthographicCamera`).

- `DirectionalLight`
- `PointLight`
- `SpotLight`

This enables type-specific payloads without variant-heavy plumbing while still supporting replacement at runtime.

### 4.3 Node Flags Interactions

Scene already defines:

- `kVisible`
- `kCastsShadows`
- `kReceivesShadows`

Policy:

- **Visibility gate (hard rule):** if the node’s effective `kVisible` is false, the light does not affect the world, even if `affects_world` is true.
- **Light contribution gate:** if the node is visible, contribution is controlled by a light property (`affects_world`).
- **Shadow casting:** a light may cast shadows only if both:
  - its own `casts_shadows` property is true, and
  - the node’s effective `kCastsShadows` is true.

Receiver note:

- `kReceivesShadows` affects whether *renderable receivers* apply shadows during shading.
  It does not affect whether a light produces shadow maps.

Rationale: `kVisible` is a scene-level on/off switch that should consistently gate all scene contributions (geometry and lights).

---

## 5. Data Model

### 5.0 Core Types, Enums, and Invariants

This section defines the “names and meanings” expected by both Scene and Renderer.

**Enums**

- `enum class LightMobility { Realtime, Mixed, Baked };`
  - `Realtime`: light participates in dynamic lighting every frame.
  - `Mixed`: participates in dynamic lighting, but may also have baked/static contributions.
  - `Baked`: excluded from dynamic direct lighting; used for baking workflows.

- `enum class ShadowResolutionHint { Low, Medium, High, Ultra };`
  A hint to the renderer; the renderer may clamp to platform budgets.

- `enum class EnvironmentComponentType { SkyAtmosphere, VolumetricClouds, Fog, SkyLight, SkySphere, PostProcessVolume };`
  Used by the SceneEnvironment persistence layer to tag environment-system records.

- `enum class AttenuationModel { InverseSquare, Linear, CustomExponent };`
  - `InverseSquare` is the physically-based default.
  - `CustomExponent` uses an authored `decay_exponent`.

**Shared structs (suggested shapes)**

- `struct ShadowSettings`
  - `float bias`
  - `float normal_bias`
  - `bool contact_shadows`
  - `ShadowResolutionHint resolution_hint`

- `struct CascadedShadowSettings`
  - `uint32_t cascade_count`
  - `std::array<float, N> cascade_distances` (or an equivalent split scheme)
  - `float distribution_exponent`

**Common numeric invariants**

- Angles are in radians.
- Directions are unit vectors.
- Ranges and radii are in world units and must be non-negative.
- When `inner_cone_angle > outer_cone_angle`, the renderer clamps/sorts (authoring error).

### 5.1 Common Light Properties (All Types)

Common properties are stored on each concrete light component and are authored in **scene-referred linear HDR**.

- `bool affects_world`
  Authoring toggle for whether the light participates in lighting.
  Note: `kVisible == false` always disables contribution regardless of this flag.

- `Vec3 color_rgb` (linear)

- `float intensity` (physically-based; HDR)
  Intensity is stored in physical-ish units per light type (documented below).
  The renderer applies exposure/tonemapping at the view/camera level.

- `LightMobility mobility` (Realtime | Mixed | Baked)
  Determines how the light participates in dynamic lighting and/or baking workflows.

- `bool casts_shadows`
  Additional authoring gate on shadow casting. Final shadow eligibility also
  requires node flag `kCastsShadows == true`.

- `ShadowSettings shadow`
  Shadow tuning knobs. A light only produces shadows if `casts_shadows == true`.
  Suggested fields:
  - `float bias` (depth bias)
  - `float normal_bias`
  - `bool contact_shadows`
  - `ShadowResolutionHint resolution_hint`

Optional (authoring/debug convenience):

- `float exposure_compensation_ev` (default 0)
  Artistic trim expressed in EV stops. Effective intensity multiplier is
  $2^{exposure\_compensation\_ev}$. This is *not* camera exposure; it is a
  per-light adjustment.

- “Affect specular” / “Affect diffuse” toggles.

#### Exposure Notes

Oxygen lighting is authored and evaluated in HDR linear space. “Exposure” is
primarily a **view/camera/post-processing** concern, not a light property.

The optional `exposure_compensation_ev` exists only to let content authors trim
a light without changing its base physical intensity, while keeping the camera
exposure pipeline coherent.

#### Intensity Units (recommended semantics)

To keep HDR lighting predictable across scenes and tools, intensity should use
consistent physically-based semantics:

- DirectionalLight: interpret `intensity` as illuminance-like scale (lux).
- PointLight / SpotLight: interpret `intensity` as luminous intensity (candela).

Note: We keep a single `float intensity` in the component for simplicity; unit
interpretation is per-type.

### 5.2 DirectionalLight

Directional lights represent “light at infinity” (sun/sky key light).

Must-have fields/behavior:

- Direction (unit vector)
  Computed from the node’s world rotation and engine convention
  `oxygen::space::move::Forward`. Exposed to the renderer as world-space
  direction.

- Angular size / soft angle
  Used to compute soft shadow penumbra and sun-disk related effects.

- Intensity and Color
  Typically interpreted as sun illuminance-like scale (HDR).

- Cascaded shadow settings
  Cascade count, distances, and distribution.

- Environment contribution toggle
  Whether the directional light contributes to ambient/sky/IBL systems.

Suggested fields (names not final; semantics are):

- `float angular_size_radians`
- `bool environment_contribution`
- `CascadedShadowSettings csm` with:
  - `uint32_t cascade_count`
  - `std::array<float, N> cascade_distances` (or equivalent scheme)
  - `float distribution_exponent` (controls cascade split distribution)

Notes:

- Directional lights are global; cascade controls are critical for scene-scale
  correctness and performance.

#### Sun integration (Directional Light)

The “sun” is represented as a **regular SceneNode** with an attached
`DirectionalLight` component. There is no special-case scene object; the
renderer *interprets* a directional light with environment contribution enabled
as the scene’s sun.

**Canonical setup**

- Create a root-level node named e.g. `Sun` and attach a `DirectionalLight`.
- Set `DirectionalLight.mobility` to:
  - `Realtime` for time-of-day (sun moves every frame), or
  - `Mixed` if direct lighting is dynamic but some baked contribution is used.
- Keep the node’s transform authoritative for sun direction:
  - The light direction is derived from the node’s world rotation.
  - The **emitted light ray direction** is the node’s world-space
    `oxygen::space::move::Forward` vector.
  - This direction is a unit vector; it represents the direction photons travel
    (from the sun toward the scene).

**Direction semantics (important for shading)**

- Scene extraction publishes `sun_dir_ws` as the direction of incoming rays
  (from light to scene). Many BRDF implementations use a vector from surface to
  light, commonly named `L`; in that case `L = -sun_dir_ws`.

**Visibility and contribution gates**

- If the node’s effective `kVisible` is false, the sun does not affect the
  world (hard rule).
- If the node is visible, `DirectionalLight.affects_world` gates participation.

**Environment / sky coupling**

- `DirectionalLight.environment_contribution` controls whether this light is
  considered the primary sun direction for:
  - sky/atmosphere rendering (if/when present),
  - ambient/IBL injection (if/when present).

**Multi-sun policy**

- The engine may support multiple directional lights for artistic reasons.
  However, for environment/sky systems there should be at most **one**
  directional light with `environment_contribution == true`.
- If multiple environment-contributing directional lights are present, the
  renderer should deterministically pick one (recommended: highest effective
  intensity after `exposure_compensation_ev`), and log a warning in debug builds.

**Shadow cascades**

- Cascaded shadow settings on `DirectionalLight` are the authoritative source
  for CSM configuration.
- Final shadow eligibility still requires:
  - node flag `kCastsShadows == true`, and
  - `DirectionalLight.casts_shadows == true`.

**Time-of-day integration (recommended approach)**

- Drive the sun by rotating the `Sun` node (azimuth/elevation) in world space.
  With Oxygen conventions (Z-up, forward = -Y), choose a mapping such that:
  - noon: rays point mostly downward along -Z,
  - sunrise/sunset: rays approach the horizon.


### 5.3 PointLight

Point lights emit from a position uniformly in all directions.

Must-have fields/behavior:

- Position (world-space) and Range
  Position is computed from node world transform. Range defines the influence
  volume.

- Intensity and Color (common fields)

- Attenuation model / decay exponent
  Linear/quadratic or physically-based inverse-square falloff; affects shader
  evaluation and culling/LOD.

- Source radius / sphere size
  Used for soft contact shadows and physically based shading (specular highlight
  shape and shadow softness).

- Shadow enable + shadow resolution hint
  Local shadow maps, potential caching policy.

Suggested fields:

- `float range`
- `AttenuationModel attenuation_model`
- `float decay_exponent` (used only when `attenuation_model == CustomExponent`)
- `float source_radius`

Notes:

- Range and attenuation are primary controls for correctness and performance;
  source radius improves realism for close contacts.

### 5.4 SpotLight

Spot lights emit from a position in a cone.

Must-have fields/behavior:

- Position and Direction
  Position is computed from node world transform. Direction is computed from
  node rotation + `oxygen::space::move::Forward`.

- Inner cone angle / Outer cone angle
  Inner = full intensity; outer = falloff boundary; used for smooth penumbra.

- Range and attenuation
  Same role as point lights.

- Source radius / soft edge
  Controls softness of the spot disk and shadow penumbra.

- Shadow parameters
  Bias, resolution hint, contact shadow toggle.

Suggested fields:

- `float range`
- `AttenuationModel attenuation_model`
- `float decay_exponent` (used only when `attenuation_model == CustomExponent`)
- `float inner_cone_angle_radians`
- `float outer_cone_angle_radians`
- `float source_radius`

Notes:

- Two-angle cone (inner/outer) is standard for creating a hard core with soft
  edges.

---

## 6. SceneNode API Shape

APIs must mirror the Camera attachment API style.

### 6.1 Attach / Detach / Replace

SceneNode exposes:

- `AttachLight(std::unique_ptr<Component> light) -> bool`
- `DetachLight() -> bool`
- `ReplaceLight(std::unique_ptr<Component> light) -> bool`
- `HasLight() -> bool`

Semantics:

- Only one light component can be attached at a time (Directional, Point, or Spot).
- `AttachLight` fails if a light already exists.
- `ReplaceLight` replaces the existing light if present; otherwise behaves like attach.
- All operations are SafeCall-based, exception-free, and resilient to invalid nodes.

### 6.2 Typed access

- `GetLight() -> std::optional<std::reference_wrapper<Component>>`
- `template<typename T> GetLightAs<T>() -> std::optional<std::reference_wrapper<T>>`
  Mirrors `SceneNode::GetCameraAs<T>()` behavior.

---

## 7. Renderer Integration

The Scene system should provide a **stable extraction point** for the renderer, without forcing a rendering technique.

Proposed concept:

- After Scene updates (flags + transforms), the engine performs a “light extraction” step that:
  - walks nodes with light components,
  - resolves world-space position/direction,
  - applies effective flags (visibility, casts shadows),
  - produces a packed list suitable for GPU upload.

The output should be a pure data view, e.g. a `SceneLightSet`:

- Directional lights list
- Point lights list
- Spot lights list

Each entry contains world-space parameters + a stable reference to the owning node handle (useful for debugging, picking, and updates).

This extraction step can later be filtered per-view (camera frustum, per-view masks) once multi-view rendering is wired in.

### 7.1 Extraction outputs (recommended)

To keep Scene and Renderer decoupled, extraction produces POD-style views:

- `SceneLightSet`
  - `std::vector<DirectionalLightGpu>`
  - `std::vector<PointLightGpu>`
  - `std::vector<SpotLightGpu>`
  - each entry includes:
    - world-space direction/position as applicable,
    - resolved `affects_world` (including the `kVisible` gate),
    - resolved shadow eligibility (including `kCastsShadows` gate),
    - the owning `NodeHandle`.

- `SceneSun`
  - optional chosen sun light (directional with `environment_contribution == true`)
  - exposes `sun_dir_ws` (incoming ray direction), color, intensity

- `SceneEnvironmentSet`
  - optional environment parameters + GPU-ready IBL resources (or handles)

### 7.2 Environment lighting and skybox

Environment lighting and the skybox are **scene-global** concepts. They should
not be modeled as node-attached lights because they:

- do not have a meaningful position in the scene graph,
- are typically evaluated as an omnidirectional function (IBL), and
- are shared across views/cameras in the same scene.

#### Representation in the Scene system

The environment is represented by a dedicated **SceneEnvironment** object that
is:

- **Not a node** (it does not exist in the scene graph).
- **Not a component of `Scene`**.
- A **Composition** in its own right, containing a variable set of
  components that implement environment systems.

The `Scene` **owns** the SceneEnvironment when one is present:

- A `Scene` may or may not have a SceneEnvironment at runtime.
- If present, the SceneEnvironment’s lifetime does not extend beyond the Scene.
- Ownership is transferred into the Scene (API takes `std::unique_ptr`).

#### Environment systems (domain) and components (implementation)

From a renderer point of view, SceneEnvironment is a collection of
environment systems. In the Scene implementation, each environment system is
represented by a component attached to the SceneEnvironment Composition.

Initial set (can evolve):

- Sky Atmosphere
- Volumetric Clouds
- Fog (Exponential Height or Volumetric)
- Sky Light (IBL)
- Sky Sphere
- Post Process Volume

Each environment system’s component is responsible for its authored parameters
and runtime behavior.

#### Scene API shape (ownership + non-owning access)

The Scene owns the environment (when present), but callers only get a
non-owning view:

- `Scene::HasEnvironment() -> bool`
- `Scene::GetEnvironment() -> observer_ptr<SceneEnvironment>` (returns `nullptr` when absent)
- `Scene::GetEnvironment() const -> observer_ptr<const SceneEnvironment>` (returns `nullptr` when absent)
- `Scene::SetEnvironment(std::unique_ptr<SceneEnvironment>) -> void` (transfers ownership into the Scene)
- `Scene::ClearEnvironment() -> void`

#### Exposure and tonemapping

- Environment lighting is authored and evaluated in **HDR linear**.
- Camera/view exposure and tonemapping are applied after lighting, consistent
  with how direct lights are handled.
- The environment component may optionally include EV compensation for author
  trim, but it must not replace camera exposure.

#### How the skybox is rendered

The skybox is a **background pass** rendered from the active camera:

- Resolve a background source from the SceneEnvironment environment systems (if present):
  - prefer **Sky Atmosphere** when present (procedural sky),
  - otherwise use **Sky Sphere** when present (textured/cubemap sky),
  - otherwise render no sky background.
- Evaluate the chosen background using the camera’s view direction.
- Apply any system-specific authored transforms/intensity (as defined by that system’s component).
- Skybox rendering is gated by:
  - a SceneEnvironment being present on the Scene, and
  - at least one sky/background system being present and enabled.

The skybox should be considered purely visual background; it does not cast
shadows. (Shadowing of sky/atmosphere is a separate feature.)

#### How environment lighting (IBL) is evaluated

The renderer extracts the SceneEnvironment once per scene update and builds
(or reuses cached) GPU resources for image-based lighting when a **Sky Light**
system is present and enabled:

- **Diffuse irradiance** (low-frequency convolution)
- **Specular prefilter** (mip chain / roughness preintegration)
- **BRDF integration LUT** (global, shared; not per-scene)

These are used during shading in addition to direct lights.

#### Coupling with the Sun

If the scene contains a directional light with
`DirectionalLight.environment_contribution == true`, it becomes the canonical
sun direction for sky/environment systems.

Examples of what this enables (implementation-dependent):

- procedural sky models (if/when present),
- optional tinting of environment ambient based on sun color/intensity,
- consistent alignment between the sun disk highlight and the skybox (when the
  skybox is authored to match a sun direction).

Policy:

- The environment system may read the chosen “sun” directional light for
  direction/color/intensity.
- The environment system must remain functional with **no** sun light present
  (e.g., indoor scenes using only an HDRI).

### 7.3 Light culling strategy (required)

Light extraction (Section 7.1) produces a scene-global set. Before shading, the
renderer must build **per-view culled light lists** to control CPU/GPU cost.

This spec intentionally does not mandate forward+/clustered vs deferred.
Instead, it specifies the **culling contract** and the data needed to implement
any of these techniques.

#### Inputs

- `SceneLightSet` (world-space parameters, effective flags already applied)
- `ViewParams` per camera/view:
  - view/projection matrices,
  - near/far,
  - viewport dimensions,
  - optional depth pyramid / HiZ (if available)

#### Per-light bounds (world space)

- Directional: infinite; cannot be frustum-culled by bounds. It is included if
  `affects_world` is true.
- Point: bounding sphere centered at `position_ws` with radius `range`.
- Spot: conservative bounds for culling:
  - recommended: bounding sphere of radius `range` at `position_ws`, or
  - tighter: bounding cone (apex = position, axis = direction, half-angle = outer
    cone, length = range) with a conservative AABB for broad-phase.

All ranges/radii are in world units and must be non-negative.

#### Broad-phase culling (required behavior)

At minimum, for each view the renderer performs:

- Frustum culling against the bounds above.
- Optional distance/importance culling (platform budget): drop lights beyond a
  max count or with negligible contribution.
  - Recommendation: use a metric based on `intensity`, attenuation at the
    closest point to the camera frustum, and `color_rgb` luminance.
  - This must be deterministic to reduce temporal flicker (stable sorting).

Directional lights are handled as a separate small set (often 0..N where N is
tiny).

#### Fine culling (optional)

- Screen-space/cluster assignment (forward+/clustered):
  - build a 2D tile or 3D cluster grid in view space,
  - assign each point/spot light to the overlapping tiles/clusters,
  - produce per-tile/per-cluster index lists.

- Deferred stencil/volume culling:
  - render light volumes (sphere/cone) into a light accumulation pass,
  - optionally use stencil to limit shading to affected pixels.

- Depth-aware rejection (HiZ):
  - use a depth pyramid to skip tiles/clusters with no geometry.

#### Outputs

The renderer builds per-view light access structures. Typical forms:

- `DirectionalLightGpu[]` (small array)
- `PointLightGpu[]` and `SpotLightGpu[]` + indices
- Either:
  - per-tile/per-cluster index lists (forward+/clustered), or
  - draw-call driven light volume rendering (deferred)

Key requirement: all GPU-facing arrays must be stable for the duration of the
frame (or until the view is re-rendered).

#### Interaction with mobility

Mobility does not change culling math, but affects *which* lights are eligible
for dynamic evaluation:

- `Realtime` and `Mixed` lights participate in runtime shading and thus in
  runtime culling.
- `Baked` lights are excluded from runtime direct lighting culling (their
  contribution is assumed to come from baked data).

### 7.4 Shader and render-pass impact (required)

This section documents the minimum shader interface expectations and how light
data is consumed by render passes.

#### GPU data and shader interfaces

Lights are consumed by shaders via GPU-resident buffers populated from the
extraction + culling steps.

Minimum required fields per GPU light entry:

- Common: `color_rgb`, `intensity`, `exposure_compensation_ev` (or pre-applied
  scale), shadow eligibility flags, and any per-light shadow settings needed
  by sampling code.
- Directional: `dir_ws` (incoming ray direction), `angular_size_radians`.
- Point: `position_ws`, `range`, `source_radius`, attenuation parameters.
- Spot: `position_ws`, `dir_ws`, `range`, `source_radius`, inner/outer angles,
  attenuation parameters.

Access pattern depends on the chosen technique:

- Clustered/forward+: pixel shader (or compute) looks up the current
  tile/cluster light list, then loops over those indices.
- Deferred: lighting may run as a full-screen pass with clustered lists, or as
  per-light volume passes; both require reading the same GPU light buffers.

Important: The shader code must treat `sun_dir_ws` as **incoming ray
direction**. If the BRDF uses a vector from surface to light named `L`, then
`L = -sun_dir_ws`.

#### Required render passes

This spec assumes the renderer has (or can add) the following lighting-related
passes. Exact technique is flexible; the required *data flow* is what matters.

- **Shadow map passes** (conditional)
  - For each light with resolved shadow eligibility:
    - Directional: cascaded shadow maps (CSM).
    - Point: omnidirectional shadowing (cubemap or dual-paraboloid; technique
      choice is renderer-specific).
    - Spot: 2D shadow map.
  - Shadow passes consume: light transform parameters + shadow settings.

- **Direct lighting evaluation pass**
  - Consumes per-view culled light access structures.
  - Implemented as:
    - forward/forward+ shading during material pass, or
    - deferred lighting after G-buffer.

- **Environment / IBL pass contribution**
  - If a SceneEnvironment is present and its relevant systems are enabled:
    - skybox background pass uses the sky system(s) (Section 7.2), and
    - shading uses irradiance/prefiltered environment + BRDF LUT.

#### Forward+ pipeline integration (Oxygen)

Oxygen uses a **Forward+** architecture. The practical implications are:

- Direct lighting is evaluated in (or alongside) the forward material shader.
- Point/Spot lights must be made available through a per-tile or per-cluster
  index structure built before the main forward shading pass.

Recommended high-level frame structure per view:

1. Ensure a depth buffer exists for the view (depth pre-pass or equivalent).
2. Run light culling + build the Forward+ light grid (compute).
3. Render the main forward material pass; for each pixel:
   - evaluate directional lights (small fixed loop),
   - fetch the tile/cluster list and loop local lights (point/spot),
   - sample shadows when eligible.

#### Shader inventory impact (Forward+)

Adding scene lights implies new shader functionality. At minimum:

- New compute shader: **BuildLightGrid**
  - Inputs: per-view matrices + viewport, `PointLightGpu[]`, `SpotLightGpu[]`.
  - Outputs:
    - a grid mapping tile/cluster -> (offset,count), and
    - a flattened light index list buffer.
  - Tile vs cluster is renderer-specific; both satisfy the culling contract.

- New/extended depth-only shaders for **shadow map passes** (as enabled):
  - Directional CSM shadow depth.
  - Spot shadow depth.
  - Point shadow depth if point shadows are supported (cubemap or other).

- Modified forward material shaders:
  - Must read the Forward+ grid/index buffers and perform bounded local-light
    loops.
  - Must implement directional light evaluation.
  - Must integrate shadow sampling paths for enabled shadow types.

The goal is to avoid requiring a distinct “lighting pass shader” in the common
case; Forward+ keeps shading in the material pass while bounding the light work.

#### Shader cost expectations

- Light loops must be bounded by culling (tile/cluster lists or volume bounds).
- Directional lights should be a small fixed upper bound.
- Shadow sampling cost is driven by:
  - cascade count (directional),
  - resolution hint and filtering choice,
  - optional contact shadows.

---

## 8. Resolved Decisions

- Intensity: physically-based semantics, HDR pipeline.
- Visibility: node invisible (`kVisible == false`) ⇒ light does not affect the world even if `affects_world` is true.
- One light per node: yes.
- Shadow ownership: both node flag `kCastsShadows` and per-light shadow settings in the light component.

---

## 9. Implementation Task List (Trackable)

This section turns the spec into an executable checklist, minimizing new
infrastructure and explicitly reusing existing Oxygen patterns:

- Scene component composition and dependencies (same as Camera components)
- SceneNode attachment API shape (same as `AttachCamera/DetachCamera/...`)
- PAK / loose-cooked scene component tables (already used for Renderables and Cameras)
- Renderer ScenePrep collection/finalization pipeline (existing traversal and per-frame state)

> Guiding rule: **don’t invent a new system** if an equivalent exists for
> cameras/renderables/scene assets.

### 9.1 Scene: Light components and APIs (mirrors Camera)

- [x] Add `DirectionalLight`, `PointLight`, `SpotLight` components under `src/Oxygen/Scene/Light/`.
  - Must use `OXYGEN_COMPONENT(...)` and require `oxygen::scene::detail::TransformComponent` (same as `PerspectiveCamera` / `OrthographicCamera`).
  - Include common authored fields: `affects_world`, `color_rgb`, `intensity`, `mobility`, `casts_shadows`, `shadow`, and optional `exposure_compensation_ev`.
- [x] Implement `UpdateDependencies(...)` for each light component to cache the owning node’s `TransformComponent*` (same pattern as Camera).
- [x] Add `SceneNode` light attachment API in `src/Oxygen/Scene/SceneNode.h/.cpp`:
  - `AttachLight(std::unique_ptr<Component>) -> bool`
  - `DetachLight() -> bool`
  - `ReplaceLight(std::unique_ptr<Component>) -> bool`
  - `HasLight() -> bool`
  - `GetLightAs<T>() -> optional<ref<T>>`
  - Semantics must match Camera behavior (null checks, supported-type checks, and “only one light per node”).
- [x] Add unit tests mirroring `src/Oxygen/Scene/Test/SceneNode_camera_test.cpp`:
  - Attach works for each type
  - Attach fails when a light already exists (including cross-type)
  - Detach removes and returns false when none
  - Replace replaces and acts like attach when none
  - `GetLightAs<T>` returns nullopt on mismatch

### 9.2 Scene-global environment (optional but specified)

This section has been revised: SceneEnvironment is **not** a `Scene` component.

- [X] Implement `SceneEnvironment` as a standalone `Composition` with components that implement a variable set of environment systems.
  - Environment systems include: Sky Atmosphere, Volumetric Clouds, Fog, Sky Light, Sky Sphere, Post Process Volume.
  - Each environment system is represented by a component with its own authored parameters.
- [X] Add `Scene` APIs with optional semantics:
  - `HasEnvironment() -> bool`
  - `GetEnvironment() -> observer_ptr<SceneEnvironment>` (returns `nullptr` when absent)
  - `SetEnvironment(std::unique_ptr<SceneEnvironment>) -> void` (transfers ownership)
  - `ClearEnvironment() -> void`
- [X] Add unit tests for Scene ↔ SceneEnvironment association semantics.

### 9.3 Persistence: PAK / loose-cooked scene component tables (no new format)

Oxygen already supports per-node component tables in cooked scenes:

- PAK schema: `SceneAssetDesc` + `SceneComponentTableDesc` in `src/Oxygen/Data/PakFormat.h`
- Loader validation: `src/Oxygen/Content/Loaders/SceneLoader.h`
- Zero-copy access: `src/Oxygen/Data/SceneAsset.h`

Tasks:

- [ ] Extend `oxygen::data::ComponentType` (FourCC) in `src/Oxygen/Data/ComponentType.h` with light kinds (e.g. `DLIT`, `PLIT`, `SLIT`).
- [ ] Persist SceneEnvironment as a **separate environment block** that follows the Scene payload in the PAK.
  - The environment block is stored immediately after the Scene descriptor payload (similar in spirit to “descriptor followed by attached payload” patterns used by other assets).
  - The environment block is always present after a Scene payload. A scene with “no environment” uses an empty header (`systems_count == 0`).
  - This layout must be documented in `src/Oxygen/Data/PakFormat.h` alongside the Scene asset format.
- [ ] Define a packed `SceneEnvironmentBlockHeader` in `src/Oxygen/Data/PakFormat.h`.
  - Must include at minimum: `systems_count`.
  - May include environment-global settings not tied to a specific system.
  - Must include enough information to parse/skip the block safely (e.g., total block byte size) while remaining forward-compatible.
- [ ] Define packed environment system record formats in `src/Oxygen/Data/PakFormat.h`.
  - Each record begins with a small record header that includes the environment system type (`EnvironmentComponentType`) and record byte size.
  - Following the record header, store that environment system’s serialized properties.
  - Unknown environment system types must be skippable using the record byte size.
- [ ] Add packed PAK record structs in `src/Oxygen/Data/PakFormat.h` for each light kind.
  - Must include `node_index` and fields needed to reconstruct component state.
  - Keep struct sizes explicit (static_assert) and stable.
- [ ] Extend `oxygen::data::SceneAsset` in `src/Oxygen/Data/SceneAsset.h` with optional access to the associated environment block.
  - Access must be separate from node component tables.
  - The accessor must expose the environment block header and a safe view over its environment-system records.
- [ ] Extend `SceneLoader` table validation in `src/Oxygen/Content/Loaders/SceneLoader.h` to recognize light tables (entry_size, node_index range, invariants).
- [ ] Extend loader validation to recognize and validate the trailing environment block.
  - Enforce that the environment block header is always present after the Scene payload.
  - Validate `systems_count` against available bytes.
  - Validate each record header (type enum range if known, record byte size non-zero, bounds).
  - Validate system-specific invariants for known types.
- [ ] Add a content/loader test proving “scene assets can carry lights” end-to-end (parse/validate) without requiring editor tooling.
  - Pattern reference: camera table emission checks in `src/Oxygen/Content/Test/FbxImporter_scene_test.cpp`.
- [ ] Add a content/loader test proving “scene assets can carry SceneEnvironment” end-to-end (parse/validate).
  - Include: empty environment block (0 components), and a populated block with multiple system records.

### 9.4 Renderer integration: extraction and publication (Stage 5A)

Renderer already expects a lighting rollout (see `src/Oxygen/Renderer/Docs/implementation_plan.md` and `src/Oxygen/Renderer/Docs/lighting_overview.md`).

- [ ] Add CPU-side extracted light snapshot types (e.g., `RendererLights` / `SceneLightSet`) in renderer code.
  - Must be POD-style and frame-stable (safe to hand to passes).
- [ ] Implement a light extraction step that:
  - walks nodes with light components,
  - resolves world-space position/direction from transforms,
  - applies the spec’s gates (`kVisible` and `kCastsShadows` interactions),
  - outputs separate directional/point/spot arrays.
- [ ] Ensure the traversal does not miss lights on nodes without `RenderableComponent`.
  - Current `ScenePrepPipeline` frame-phase traversal skips nodes without renderables; update the pipeline or add a parallel light traversal so “light-only” nodes still contribute.

### 9.5 Renderer: SceneConstants + single-light shading (Stage 5B)

- [ ] Extend `src/Oxygen/Renderer/Types/SceneConstants.h` GPU payload with minimal lighting constants:
  - ambient
  - one directional light (dir/color/intensity)
  - `num_lights` and bindless slots as needed by later stages
- [ ] Populate constants from extraction deterministically (e.g., “first enabled directional”).
- [ ] Update the forward shading path to consume these constants and render a lit result.

### 9.6 Renderer: multi-light bindless buffer (Stage 5C) and culling (later)

- [ ] Add bindless structured buffer for lights and upload per-frame extracted arrays.
  - Follow bindless conventions: no new root bindings; use a bindless slot index.
- [ ] Shader loops over `min(num_lights, MAX_LIGHTS_PS)` for correctness (no culling yet).
- [ ] Implement per-view culling + Forward+ light grid (compute) once correctness is established.

### 9.7 Shadows (incremental)

- [ ] Plumb shadow settings through extraction and GPU payloads even if shadow passes are stubbed.
- [ ] Implement shadow passes incrementally:
  - Directional CSM first
  - Spot shadows next
  - Point shadows last (technique choice is renderer-specific)

### 9.8 Example & sanity validation

- [ ] Add/update an example scene that includes:
  - one directional “Sun” node
  - several point lights
  - a moving camera
- [ ] Validate visually:
  - direction convention (`Forward`)
  - `kVisible` hard-gates contribution
  - deterministic behavior across frames
