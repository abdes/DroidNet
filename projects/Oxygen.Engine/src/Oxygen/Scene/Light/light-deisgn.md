
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

**Enums**:

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

**Shared structs (suggested shapes)**:

- `struct ShadowSettings`
  - `float bias`
  - `float normal_bias`
  - `bool contact_shadows`
  - `ShadowResolutionHint resolution_hint`

- `struct CascadedShadowSettings`
  - `uint32_t cascade_count`
  - `std::array<float, N> cascade_distances` (or an equivalent split scheme)
  - `float distribution_exponent`

**Common numeric invariants**:

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

**Canonical setup**:

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

**Direction semantics (important for shading)**:

- Scene extraction publishes `sun_dir_ws` as the direction of incoming rays
  (from light to scene). Many BRDF implementations use a vector from surface to
  light, commonly named `L`; in that case `L = -sun_dir_ws`.

**Visibility and contribution gates**:

- If the node’s effective `kVisible` is false, the sun does not affect the
  world (hard rule).
- If the node is visible, `DirectionalLight.affects_world` gates participation.

**Environment / sky coupling**:

- `DirectionalLight.environment_contribution` controls whether this light is
  considered the primary sun direction for:
  - sky/atmosphere rendering (if/when present),
  - ambient/IBL injection (if/when present).

**Multi-sun policy**:

- The engine may support multiple directional lights for artistic reasons.
  However, for environment/sky systems there should be at most **one**
  directional light with `environment_contribution == true`.
- If multiple environment-contributing directional lights are present, the
  renderer should deterministically pick one (recommended: highest effective
  intensity after `exposure_compensation_ev`), and log a warning in debug builds.

**Shadow cascades**:

- Cascaded shadow settings on `DirectionalLight` are the authoritative source
  for CSM configuration.
- Final shadow eligibility still requires:
  - node flag `kCastsShadows == true`, and
  - `DirectionalLight.casts_shadows == true`.

**Time-of-day integration (recommended approach)**:

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

## 7. SceneEnvironment (Scene-side)

Environment lighting and the sky/background are **scene-global** concepts.
They are not modeled as node-attached lights because they:

- do not have a meaningful position in the scene graph,
- are typically evaluated as an omnidirectional function (IBL), and
- are shared across views/cameras in the same scene.

### 7.1 Representation in the Scene system

The environment is represented by a dedicated **SceneEnvironment** object that
is:

- **Not a node** (it does not exist in the scene graph).
- **Not a component of `Scene`**.
- A **Composition** in its own right, containing a variable set of components
  that implement environment systems.

The `Scene` **owns** the SceneEnvironment when one is present:

- A `Scene` may or may not have a SceneEnvironment at runtime.
- If present, the SceneEnvironment’s lifetime does not extend beyond the Scene.
- Ownership is transferred into the Scene (API takes `std::unique_ptr`).

### 7.2 Environment systems

Each environment system is represented by a component attached to the
SceneEnvironment Composition.

Initial set (can evolve):

- Sky Atmosphere
- Volumetric Clouds
- Fog (Exponential Height or Volumetric)
- Sky Light (IBL)
- Sky Sphere
- Post Process Volume

### 7.3 Scene API shape (ownership + non-owning access)

The Scene owns the environment (when present), but callers only get a
non-owning view:

- `Scene::HasEnvironment() -> bool`
- `Scene::GetEnvironment() -> observer_ptr<SceneEnvironment>` (returns `nullptr` when absent)
- `Scene::GetEnvironment() const -> observer_ptr<const SceneEnvironment>` (returns `nullptr` when absent)
- `Scene::SetEnvironment(std::unique_ptr<SceneEnvironment>) -> void` (transfers ownership into the Scene)
- `Scene::ClearEnvironment() -> void`

Renderer integration details (extraction, culling, GPU formats, shading, and
passes) are documented in `src/Oxygen/Renderer/Docs/lighting_overview.md`.

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

- [x] Extend `oxygen::data::ComponentType` (FourCC) in `src/Oxygen/Data/ComponentType.h` with light kinds (e.g. `DLIT`, `PLIT`, `SLIT`).
- [x] Persist SceneEnvironment as a **separate environment block** that follows the Scene payload in the PAK.
  - The environment block is stored immediately after the Scene descriptor payload (similar in spirit to “descriptor followed by attached payload” patterns used by other assets).
  - The environment block is always present after a Scene payload. A scene with “no environment” uses an empty header (`systems_count == 0`).
  - This layout must be documented in `src/Oxygen/Data/PakFormat.h` alongside the Scene asset format.
- [x] Define a packed `SceneEnvironmentBlockHeader` in `src/Oxygen/Data/PakFormat.h`.
  - Must include at minimum: `systems_count`.
  - May include environment-global settings not tied to a specific system.
  - Must include enough information to parse/skip the block safely (e.g., total block byte size) while remaining forward-compatible.
- [x] Define packed environment system record formats in `src/Oxygen/Data/PakFormat.h`.
  - Each record begins with a small record header that includes the environment system type (`EnvironmentComponentType`) and record byte size.
  - Following the record header, store that environment system’s serialized properties.
  - Unknown environment system types must be skippable using the record byte size.
- [x] Add packed PAK record structs in `src/Oxygen/Data/PakFormat.h` for each light kind.
  - Must include `node_index` and fields needed to reconstruct component state.
  - Keep struct sizes explicit (static_assert) and stable.
- [x] Extend `oxygen::data::SceneAsset` in `src/Oxygen/Data/SceneAsset.h` with optional access to the associated environment block.
  - Access must be separate from node component tables.
  - The accessor must expose the environment block header and a safe view over its environment-system records.
- [x] Extend `SceneLoader` table validation in `src/Oxygen/Content/Loaders/SceneLoader.h` to recognize light tables (entry_size, node_index range, invariants).
- [x] Extend loader validation to recognize and validate the trailing environment block.
  - Enforce that the environment block header is always present after the Scene payload.
  - Validate `systems_count` against available bytes.
  - Validate each record header (type enum range if known, record byte size non-zero, bounds).
  - Validate system-specific invariants for known types.
- [x] Add a content/loader test proving “scene assets can carry lights” end-to-end (parse/validate) without requiring editor tooling.
  - Pattern reference: camera table emission checks in `src/Oxygen/Content/Test/FbxImporter_scene_test.cpp`.
- [x] Add a content/loader test proving “scene assets can carry SceneEnvironment” end-to-end (parse/validate).
  - Include: empty environment block (0 components), and a populated block with multiple system records.

> Renderer-side lighting work (extraction, culling, GPU formats, shading, and
> validation) is tracked in `src/Oxygen/Renderer/Docs/lighting_overview.md` and
> `src/Oxygen/Renderer/Docs/implementation_plan.md`.
