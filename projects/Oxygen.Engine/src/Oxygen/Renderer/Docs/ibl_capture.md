# IBL Capture: Probes + DXR (Pluggable, Coroutine RenderGraph-Friendly)

**Status**: Implementation-ready design

**Last updated**: January 2026

This document defines a renderer architecture for **Image Based Lighting (IBL) capture** that is:

- **Probe-centric** (baseline, predictable, stable)
- **DXR-ready** (future path for high quality capture; not in scope for initial implementation)
- **Coroutine RenderGraph-friendly** (no rigid graph; explicit integration points for app coroutines)
- **Fully pluggable** (capture backends, parallax strategies, placement strategies — all runtime-selectable)
- **100% bindless** (fits the engine's ABI and descriptor heap model; no legacy binding paths)

It deliberately does **not** treat SSR as part of the lighting pipeline.
SSR remains a **post/resolve** technique that can optionally *augment* specular reflections, but it is not a substitute for a complete IBL capture solution.

---

## 1. Design Principles (Oxygen Alignment)

This design adheres to Oxygen's core architectural principles:

### 1.1 100% Bindless

- All probe resources (cubemaps, irradiance maps, prefilter maps, depth cubemaps, SDF volumes) are accessed via **bindless SRV indices**.
- Probe metadata is stored in a **bindless structured buffer** indexed from `EnvironmentStaticData`.
- No per-draw root descriptor table switching for probe resources.

### 1.2 Coroutine-Based Render Graph

- Oxygen's render graph is a **coroutine** (`co::Co<>`), not a declarative DAG.
- Probe capture/filter steps are **explicit async operations** that the app coroutine invokes at the appropriate point.
- No hidden subgraphs or implicit scheduling; the app controls when and how much probe work runs.

### 1.3 Runtime Pluggability (No Preprocessor Divergence)

- Capture backends (Raster, DXR) are **runtime-registered** and selected based on device capabilities + user config.
- Parallax correction strategies are **runtime-selectable** per probe.
- Placement strategies are **application-provided** and invoked from the app layer.
- No `#ifdef` guards for feature selection; all code paths are compiled and dispatched at runtime.

### 1.4 Strategy Pattern Throughout

- Capture: `IProbeCaptureBackend`
- Parallax: `IParallaxCorrectionStrategy`
- Placement: `IProbePlacementStrategy`
- Baking: `IProbeBakeStrategy`

This keeps the renderer core stable while allowing techniques to evolve independently.

---

## 2. Motivation / Problem Statement

The current forward PBR shading samples a **cubemap-based environment** for specular reflections.
This means a reflective object (sphere) reflects the **sky cubemap** even where it should reflect nearby scene geometry (cube below).

That behavior is correct for IBL *as implemented*: the “environment” is infinitely far.
To reflect local geometry in a stable, complete way, the renderer must provide **local reflection probes** (or an equivalent complete-capture approach).

---

## 3. Goals

| ID | Goal | Scope |
|----|------|-------|
| G1 | Support **multiple reflection probes** with per-probe captured cubemaps | Core |
| G2 | Support **pluggable capture backends** (Raster baseline; DXR future) | Core |
| G3 | Make capture mode **selectable by the application render graph coroutine** | Core |
| G4 | Make parallax correction **strategy-pluggable** (box projection, depth cubemap, SDF, …) | Core |
| G5 | Make probe placement + baking **strategy-pluggable** (artist placed, 3D grid, …) | Core |
| G6 | Preserve the current **100% bindless ABI** | Constraint |
| G7 | Prepare for **DXR capture backend** without implementing it now | Future-ready |

---

## 4. Non-Goals

- Implement SSR (belongs in post-processing pipeline).
- Implement a full post stack (TAA, tonemap, bloom).
- Implement DXR capture backend in initial phase (infrastructure-ready only).
- Implement "perfect" parallax correctness — design supports multiple techniques; box projection is the baseline.
- Implement full-featured GI (LightProbe reserved for future).

---

## 5. Constraints / ABI Alignment

The probe system must integrate with the existing bindless ABI:

| Binding | Slot | Content |
|---------|------|---------|
| Root CBV | `b1` | `SceneConstants` (contains `bindless_env_static_slot`) |
| Root CBV | `b3` | `EnvironmentDynamicData` |
| Bindless SRV | via `bindless_env_static_slot` | `EnvironmentStaticData` (extended with probe fields) |
| Bindless SRV | via `bindless_reflection_probes_slot` | `ProbeGpu[]` structured buffer |

All probe cubemaps, irradiance maps, prefilter maps, and parallax resources are accessed via **bindless SRV indices** stored in `ProbeGpu`.

---

## 6. Existing Relevant Infrastructure (Today)

This plan builds on what already exists:

- **EnvironmentStaticDataManager**
  - Resolves and publishes the SkyLight cubemap slot.
  - Queries an `IIblProvider` to obtain `irradiance_map_slot` and `prefilter_map_slot`.
- **IblComputePass**
  - Runs compute filtering from a source cubemap into irradiance/prefilter textures.
  - Uses bindless SRV/UAV indices.
  - Currently assumes a small maximum number of dispatch constants (`kMaxDispatches`).
- **SkyCapturePass**
  - Exists but is currently a stub (“not implemented”).

The key missing piece is a **multi-probe capture + storage + selection** system.

---

## 7. High-Level Architecture

The proposed architecture is split into six major roles:

1. **Probe Authoring (Scene)**
   - A set of probe entities defines where to capture the environment and how it influences shading. This includes a shared `Probe` base component and derived components like `ReflectionProbe` and `LightProbe`.

2. **Probe Registry / Cache (Renderer-owned)**
   - Tracks probe lifetime, dirty state, GPU resources, and bindless slots.

3. **Capture Backend (Pluggable)**
   - Raster capture backend: renders the scene into a cubemap for each probe.
   - DXR capture backend: ray-traces the scene into a cubemap for each probe.

4. **Filtering Backend (Reusable)**
   - Convolves each captured cubemap into diffuse irradiance + specular prefilter cubemaps.
   - Reuses and generalizes the existing `IblComputePass` path.

5. **Probe Placement + Baking Strategies (Pluggable)**
   - Artist placed probes (scene-authored).
   - Automatic grid probes (configurable density).
   - Future: adaptive placement based on scene analysis.

6. **Parallax Correction Strategies (Pluggable, shader-facing)**
   - Box projection (baseline).
   - Depth cubemap parallax.
   - SDF raymarching (or other volumetric techniques).

Finally, the forward shading pass selects a probe (or blends multiple) and samples the correct cubemap(s) using the chosen parallax correction strategy.

---

## 8. Scene Data Model (Authoring)

This section defines the **scene-side** probe data model: what data is authored, how it integrates with the existing scene architecture, and the precise boundary between authoring (scene) and runtime (renderer) responsibilities.

### 8.1 Design Rationale

Reflection probes are **spatial environment capture anchors**. They define:

- **Where** to capture the environment (position + orientation).
- **What volume** the captured data represents (influence shape).
- **How** to sample from the captured data (parallax correction).

Probes are **not** renderables—they have no geometry. They participate in the scene graph only for transform hierarchy and spatial queries.

**Separation of concerns**:

| Responsibility | Owner | Data |
|----------------|-------|------|
| Authored probe properties | Scene (``Probe`` component) | Position, influence shape, capture settings |
| GPU probe list + bindless slots | Renderer (``ReflectionProbeManager``) | ``ProbeGpu[]`` structured buffer |
| Capture scheduling + execution | Renderer / App coroutine | Frame budget, dirty tracking |
| Placement generation | Application strategy | Generates scene nodes with probe components |

This mirrors Oxygen's existing pattern where ``DirectionalLight`` is a scene component, but ``DirectionalLightManager`` (renderer) owns the GPU upload and slot assignment.

---

### 8.2 Component Architecture

Probes integrate with the scene using Oxygen's ``Component`` system. A ``SceneNode`` can host a probe component; the probe's world transform is derived from the node's ``TransformComponent``.

#### 8.2.1 Inheritance Hierarchy

```
Component (oxygen::Component)
└── ProbeBase
    ├── ReflectionProbe   (specular + diffuse IBL)
    └── LightProbe        (reserved for future GI)
```

``ProbeBase`` is an **abstract base** containing shared authored properties. Derived types add type-specific fields and may have different renderer-side behaviors.

#### 8.2.2 Component Dependencies

```cpp
class ProbeBase : public Component {
  OXYGEN_COMPONENT(ProbeBase)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)
  // ...
};
```

The ``TransformComponent`` dependency ensures probe world position/orientation is always available.

---

### 8.3 ``ProbeBase`` — Shared Authored Properties

All probe types share these authored properties. The design prioritizes clarity over compactness; the renderer compresses data for GPU upload.

#### 8.3.1 Influence Volume

Defines the spatial region where this probe's data is valid for shading.

```cpp
enum class ProbeShape : std::uint8_t {
  kSphere = 0,
  kBox = 1,
};

struct ProbeInfluence {
  ProbeShape shape = ProbeShape::kBox;
  float radius = 5.0F;              // Used when shape == kSphere
  glm::vec3 extents { 5.0F };       // Half-size; used when shape == kBox
  glm::vec3 offset { 0.0F };        // Local offset from node origin
};
```

**Design note**: The ``offset`` allows the influence volume center to differ from the capture origin (node position). This is useful when a probe is attached to a wall but should influence a room volume.

#### 8.3.2 Capture Settings

```cpp
enum class ProbeUpdatePolicy : std::uint8_t {
  kStatic = 0,     // Capture once at load/bake time
  kOnDemand = 1,   // Capture when explicitly requested
  kRealtime = 2,   // Recapture every N frames (budget permitting)
};

enum class ProbeCaptureBackend : std::uint8_t {
  kAuto = 0,       // Renderer chooses best available
  kRaster = 1,     // Force rasterization capture
  kDxr = 2,        // Force DXR capture (if available)
};

struct ProbeCaptureFlags {
  bool include_sky : 1 = true;
  bool include_opaque : 1 = true;
  bool include_emissive : 1 = true;
  bool include_transparent : 1 = false;  // Future
};

struct ProbeCaptureSettings {
  std::uint16_t face_resolution = 128;  // Per-face resolution (power of 2)
  ProbeUpdatePolicy update_policy = ProbeUpdatePolicy::kStatic;
  ProbeCaptureBackend backend_preference = ProbeCaptureBackend::kAuto;
  ProbeCaptureFlags capture_flags {};
};
```

#### 8.3.3 Parallax Correction

```cpp
enum class ParallaxMode : std::uint8_t {
  kNone = 0,           // Infinite environment assumption
  kBoxProjection = 1,  // Box-projected cubemap
  kDepthCubemap = 2,   // Depth-based parallax (requires depth capture)
  kSdfRaymarch = 3,    // SDF volume intersection (requires SDF data)
};

struct ProbeParallaxSettings {
  ParallaxMode mode = ParallaxMode::kBoxProjection;
  glm::vec3 box_extents { 5.0F };  // Projection box half-size (if mode == kBoxProjection)
  glm::vec3 box_offset { 0.0F };   // Projection box center offset from capture origin
};
```

**Design note**: The parallax box can differ from the influence volume. The influence volume controls *which* probe is selected; the parallax box controls *how* the cubemap is sampled.

#### 8.3.4 Priority + Blending

```cpp
struct ProbeBlendSettings {
  std::int16_t priority = 0;           // Higher = preferred when overlapping
  float blend_distance = 1.0F;         // Fade distance at influence boundary (meters)
};
```

#### 8.3.5 Complete ``ProbeBase`` Class

```cpp
class ProbeBase : public Component {
  OXYGEN_COMPONENT(ProbeBase)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  // --- Influence ---
  auto SetInfluence(const ProbeInfluence& influence) noexcept -> void;
  [[nodiscard]] auto GetInfluence() const noexcept -> const ProbeInfluence&;

  // --- Capture ---
  auto SetCaptureSettings(const ProbeCaptureSettings& settings) noexcept -> void;
  [[nodiscard]] auto GetCaptureSettings() const noexcept -> const ProbeCaptureSettings&;

  // --- Parallax ---
  auto SetParallaxSettings(const ProbeParallaxSettings& settings) noexcept -> void;
  [[nodiscard]] auto GetParallaxSettings() const noexcept -> const ProbeParallaxSettings&;

  // --- Blend ---
  auto SetBlendSettings(const ProbeBlendSettings& settings) noexcept -> void;
  [[nodiscard]] auto GetBlendSettings() const noexcept -> const ProbeBlendSettings&;

  // --- Enable/Disable ---
  auto SetEnabled(bool enabled) noexcept -> void;
  [[nodiscard]] auto IsEnabled() const noexcept -> bool;

protected:
  ProbeBase() = default;
  ~ProbeBase() override = default;
  OXYGEN_DEFAULT_COPYABLE(ProbeBase)
  OXYGEN_DEFAULT_MOVABLE(ProbeBase)

  auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept -> void override;

private:
  bool enabled_ = true;
  ProbeInfluence influence_ {};
  ProbeCaptureSettings capture_ {};
  ProbeParallaxSettings parallax_ {};
  ProbeBlendSettings blend_ {};
  detail::TransformComponent* transform_ = nullptr;
};
```

---

### 8.4 ``ReflectionProbe`` — Specular + Diffuse IBL

``ReflectionProbe`` is the primary probe type for PBR specular and diffuse indirect lighting.

```cpp
class ReflectionProbe final : public ProbeBase {
  OXYGEN_COMPONENT(ReflectionProbe)

public:
  ReflectionProbe() = default;
  ~ReflectionProbe() override = default;
  OXYGEN_DEFAULT_COPYABLE(ReflectionProbe)
  OXYGEN_DEFAULT_MOVABLE(ReflectionProbe)

  //! When true, this probe serves as the global IBL fallback
  //! (used when SkyLight source is kCapturedScene).
  auto SetGlobalFallback(bool is_global) noexcept -> void { is_global_fallback_ = is_global; }
  [[nodiscard]] auto IsGlobalFallback() const noexcept -> bool { return is_global_fallback_; }

private:
  bool is_global_fallback_ = false;
  // Future: per-probe roughness bias, importance, layer mask, etc.
};
```

---

### 8.5 ``LightProbe`` — Reserved for GI (Future)

``LightProbe`` is reserved for diffuse global illumination (irradiance volumes, spherical harmonics). It shares the same base class for placement/baking infrastructure reuse.

```cpp
class LightProbe final : public ProbeBase {
  OXYGEN_COMPONENT(LightProbe)

public:
  LightProbe() = default;
  ~LightProbe() override = default;
  OXYGEN_DEFAULT_COPYABLE(LightProbe)
  OXYGEN_DEFAULT_MOVABLE(LightProbe)

  // Reserved fields for GI:
  // - SH order
  // - irradiance grid participation
  // - etc.
};
```

---

### 8.6 Global Environment Capture Origin

When `SkyLight` is set to `SkyLightSource::kCapturedScene`, the renderer needs a capture origin for the global IBL fallback.

#### 8.6.1 What Modern Engines Do

| Engine | Approach |
|--------|----------|
| **Unreal Engine 5** | `SkyLight` actor IS the capture origin. It's placed in the scene with a transform. Effectively a special probe. |
| **Unity** | A `Reflection Probe` with "Type = Baked" at scene center, or the first probe in the hierarchy, serves as global fallback. |
| **Frostbite** | Environment probes with one designated as "global" or "fallback" via priority/flag. |
| **CryEngine** | One probe marked as the default environment probe. |

**Pattern**: The global IBL source is a **probe** (or probe-like entity) with a special designation—not a separate concept.

#### 8.6.2 Recommended Approach for Oxygen

**Add an `is_global_fallback` flag to `ReflectionProbe`.**

```cpp
class ReflectionProbe final : public ProbeBase {
  OXYGEN_COMPONENT(ReflectionProbe)

public:
  // ... existing ...

  //! When true, this probe serves as the global IBL fallback.
  //! Only one probe should have this flag set; if multiple exist,
  //! the highest-priority one wins.
  auto SetGlobalFallback(bool is_global) noexcept -> void;
  [[nodiscard]] auto IsGlobalFallback() const noexcept -> bool;

private:
  bool is_global_fallback_ = false;
};
```

**Why this is correct**:

1. **Unified mental model** — artists place probes; one is marked "global". No separate anchor concept.
2. **Reuses infrastructure** — the global probe uses the same capture, filtering, and parallax pipeline.
3. **Matches industry practice** — Unreal/Unity/Frostbite all treat the global IBL as "just a probe with a flag".
4. **Clean fallback** — if no probe is marked global, the renderer can auto-select the highest-priority probe or use a default sky-only capture.

**Renderer behavior**:

```cpp
// In ReflectionProbeManager::CollectFromScene()
ReflectionProbe* global_probe = nullptr;
for (auto& probe : all_probes) {
  if (probe.IsGlobalFallback() && probe.IsEnabled()) {
    if (!global_probe || probe.GetBlendSettings().priority > global_probe->GetBlendSettings().priority) {
      global_probe = &probe;
    }
  }
}
// global_probe is used for SkyLight::kCapturedScene
```

**Do NOT add a separate `SkyLightCaptureAnchor` component** — this fragments the probe concept and creates confusion about which system "owns" the global IBL.

---

### 8.7 Probe Placement Strategies

Placement strategies determine **which probes exist** in a scene. They are **application-owned** and operate on the scene graph.

#### 8.7.1 Interface

```cpp
class IProbePlacementStrategy {
public:
  virtual ~IProbePlacementStrategy() = default;

  //! Generate or update probes in the scene.
  //! Called by the application before rendering when probe layout may change.
  virtual void GenerateProbes(scene::Scene& scene) = 0;

  //! Optional: remove probes this strategy previously generated.
  virtual void ClearProbes(scene::Scene& scene) { /* no-op by default */ }
};
```

#### 8.7.2 Built-in Strategies

| Strategy | Description |
|----------|-------------|
| ``ArtistPlacedStrategy`` | No-op; probes are manually authored in the scene. |
| ``GridPlacementStrategy`` | Generates a uniform 3D grid of probes within a bounding volume. |
| ``AdaptivePlacementStrategy`` | (Future) Analyzes scene geometry to place probes at optimal locations. |

**Grid3D parameters** (example):

```cpp
struct Grid3DPlacementParams {
  glm::vec3 min_bounds { -50.0F };
  glm::vec3 max_bounds { 50.0F };
  glm::vec3 spacing { 10.0F };
  std::uint32_t max_probes = 64;
  ProbeInfluence default_influence {};
  ProbeCaptureSettings default_capture {};
};
```

---

### 8.8 Probe Baking Strategies

Baking strategies determine **when and how** probe data is generated. They are **renderer-adjacent** but may be application-controlled.

#### 8.8.1 Interface

```cpp
using ProbeId = std::uint32_t;

class IProbeBakeStrategy {
public:
  virtual ~IProbeBakeStrategy() = default;

  //! Mark a probe as needing recapture.
  virtual void MarkDirty(ProbeId probe) = 0;

  //! Select which probes to update this frame given a budget.
  //! Returns a list of probe IDs in priority order.
  virtual auto SelectWork(std::uint32_t max_probes) -> std::vector<ProbeId> = 0;
};
```

#### 8.8.2 Built-in Strategies

| Strategy | Description |
|----------|-------------|
| ``StaticBakeStrategy`` | All probes captured once at load; no runtime updates. |
| ``PriorityBakeStrategy`` | Probes sorted by priority + distance to camera; budget-limited. |
| ``StreamingBakeStrategy`` | (Future) Integrates with level streaming; captures on cell load. |

---

### 8.9 Serialization

Probe components serialize with the scene graph using Oxygen's standard component serialization. Key points:

- Probe properties are authored data; serialized to scene file.
- GPU resources (cubemaps, filtered maps) are **not** serialized—they are regenerated by the renderer.
- Baked probe data for static probes can be serialized separately (asset cache) to avoid runtime capture.

---

### 8.10 Summary: Authored vs Runtime Data

| Data | Location | Serialized | Notes |
|------|----------|------------|-------|
| Probe transform | ``SceneNode`` | ✓ | Via ``TransformComponent`` |
| Influence volume | ``ProbeBase`` | ✓ | Shape, extents, offset |
| Capture settings | ``ProbeBase`` | ✓ | Resolution, policy, flags |
| Parallax settings | ``ProbeBase`` | ✓ | Mode, box extents |
| Blend settings | ``ProbeBase`` | ✓ | Priority, blend distance |
| GPU probe list | ``ReflectionProbeManager`` | ✗ | Built at runtime |
| Source cubemaps | ``ReflectionProbeIblManager`` | ✗ (or cache) | Captured/loaded at runtime |
| Filtered IBL maps | ``ReflectionProbeIblManager`` | ✗ (or cache) | Filtered at runtime |
| Bindless SRV slots | Renderer | ✗ | Assigned at runtime |

---
## 9. GPU Data Contract

### 9.1 New GPU Payload: `ProbeGpu`

Add a new bindless SRV structured buffer containing all active probes.
Each element contains (minimum):

- World position
- Influence shape (box extents or sphere radius)
- Parallax correction parameters (shape + transforms + technique-specific resources)
- Bindless SRV slots:
  - `source_cubemap_slot` (optional; mainly for debug)
  - `irradiance_map_slot`
  - `prefilter_map_slot`
- Priority

To support pluggable parallax correction cleanly, include these optional fields (may be invalid indices when unused):

- `parallax_mode` (enum)
- `depth_cubemap_slot` (SRV; for depth-cubemap parallax)
- `sdf_volume_slot` (SRV; for SDF raymarch parallax)
- `parallax_params_slot` (SRV; optional structured params blob)

### 9.2 Extending `EnvironmentStaticData`

Add probe-related fields to `EnvironmentStaticData`:

- `bindless_reflection_probes_slot` (SRV slot for probe list)
- `reflection_probe_count`
- `global_probe_index` (optional: index of the SkyLight/global probe)

Rationale:

- Keeps the root ABI unchanged (still accessed via `bindless_env_static_slot`).
- Allows forward shaders to access probe list safely and deterministically.

### 9.3 Shader Selection + Parallax Correction

In the forward shader:

1. Find best probe(s) for the shaded world position.
2. Compute reflection direction `R`.
3. Apply the selected **parallax correction strategy**.
4. Sample prefiltered specular and irradiance diffuse.

Baseline selection algorithm:

- Choose highest priority probe that contains point.
- If none contain point, choose nearest probe within a max distance.

Higher quality (future):

- Blend up to 2–4 probes with weights.

---

## 10. Parallax Correction (Strategy-Pluggable Design)

Parallax correction must be selectable **without** preprocessor switches and must remain extensible.
Some techniques require **additional inputs** (e.g., depth cubemap, SDF volume) and may come with their own shaders and generation passes.

This design therefore treats parallax correction as a strategy with two sides:

- **Data production (CPU + passes)**: generate any required per-probe resources.
- **Sampling (HLSL)**: use those resources to parallax-correct the sampling direction and/or derive an effective hit point.

### 10.1 HLSL Strategy Dispatch

Implement a single entry point used by forward shading:

```hlsl
struct ParallaxResult {
  float3 sampleDir;     // direction used for cubemap sampling
  float  visibility;    // optional multiplier (e.g., 0 if ray exits volume)
};

ParallaxResult ApplyParallaxCorrection(
  ProbeGpu probe,
  float3 worldPos,
  float3 reflectDirWs);
```

Strategy selection is per-probe via `probe.parallax_mode`:

- `kNone`: `sampleDir = reflectDirWs`
- `kBoxProjected`: classic box-projected cubemap direction
- `kDepthCubemap`: intersect reflection ray with probe volume, then use depth cubemap to refine hit distance
- `kSdfRaymarch`: raymarch an SDF volume to find intersection point inside the probe influence
- `kCustom`: reserve for application-defined modes (e.g., by mapping to a params slot)

Important: strategies are allowed to consume extra probe-bound resources (depth cubemap, SDF volume, params buffers) and may use additional helper shader code.

### 10.2 Resource Ownership and Validity

Each strategy has different data needs:

- Box projection: requires probe box transform/extents.
- Depth cubemap: requires `depth_cubemap_slot` and a defined depth encoding convention.
- SDF raymarch: requires `sdf_volume_slot` (and sampling scale/transform).

The `ProbeGpu` payload must therefore carry **technique-specific SRV slots** and the shader must guard invalid indices.

This is intentional: it keeps the forward shader’s call site stable, while allowing new techniques to be plugged in by adding:

- a new `parallax_mode` value
- a new SRV slot field (if needed)
- a new HLSL implementation branch

### 10.3 Why This Structure

- Keeps forward shading simple: “select probe → apply parallax → sample IBL”.
- Makes it straightforward to add a new parallax technique without touching the probe selection logic.
- Keeps radiance capture/filtering largely independent from parallax technique.

Some techniques *do* require extra capture outputs (e.g., depth cubemap alongside radiance). This is handled explicitly via optional per-probe resources owned by the probe cache.

### 10.4 Parallax Techniques That Require Additional Inputs

To support multiple techniques cleanly, the renderer must allow a parallax strategy to request and populate additional per-probe resources.

#### 10.4.1 Additional Resources (Examples)

- **Depth Cubemap Parallax**
  - Resource: `depth_cubemap_slot` (cubemap storing linear depth or hit distance)
  - Producer: capture pass that renders depth from the probe origin (raster) or writes ray hit distance (DXR)
  - Consumer: shader that refines intersection distance along the reflection ray

- **SDF Raymarch Parallax**
  - Resource: `sdf_volume_slot` (3D texture or sparse structure)
  - Producer: offline bake or runtime build (scene voxelization / SDF generation)
  - Consumer: shader raymarch in probe space

#### 10.4.2 Strategy Interface (Renderer/App Integration)

Define a parallax strategy interface that can participate in:

- resource provisioning (allocate per-probe resources)
- generation scheduling (which probes need updates)
- shader hookup (which HLSL implementation corresponds to each mode)

```cpp
struct ParallaxResourceNeeds {
  bool needs_depth_cubemap = false;
  bool needs_sdf_volume = false;
  bool needs_params_buffer = false;
};

class IParallaxCorrectionStrategy {
public:
  virtual ~IParallaxCorrectionStrategy() = default;
  virtual const char* Name() const noexcept = 0;
  virtual ParallaxMode Mode() const noexcept = 0;
  virtual ParallaxResourceNeeds Needs() const noexcept = 0;

  // Called when probe resources are (re)allocated.
  virtual void EnsureResources(ProbeId probe, ReflectionProbeIblManager& cache) = 0;

  // Optionally schedule generation work (depth capture, SDF update, etc).
  virtual void MarkDirty(ProbeId probe) = 0;

  // Called from the app render graph coroutine at the appropriate time.
  virtual co::Co<> Generate(
    graphics::CommandRecorder& recorder,
    IProbeCaptureBackend& capture_backend,
    std::span<const ProbeId> probes_to_update) = 0;

  // Populate GPU payload fields (slots + params) for shader consumption.
  virtual void PopulateGpuPayload(ProbeId probe, ProbeGpu& out) = 0;
};
```

Notes:

- This keeps DXR vs raster selection orthogonal: the parallax strategy can reuse the chosen capture backend when it needs depth/hit distance.
- Strategies can be registered at runtime (no preprocessor divergence).
- The shader side dispatch remains `probe.parallax_mode`.

---

## 11. RenderGraph Integration (Coroutine-Friendly)

### 11.1 Integration Points (What the App Render Graph Coroutine Does)

Oxygen’s render graph is a coroutine and can branch/compose arbitrarily.
To integrate probe capture efficiently without implying a rigid graph, the renderer should expose a small set of **explicit steps** that the application can call from its coroutine:

1. **Collect** probes from the scene (artist probes and/or generated probes).
2. **Schedule** probe work (budgeting + dirty tracking).
3. **Capture** source cubemaps for scheduled probes (backend-dependent).
4. **Filter** to irradiance/prefilter cubemaps.
5. **Upload/Publish** probe list GPU buffer + bindless slots for shading.

These steps can be inserted before the main `ShaderPass`, or earlier if other passes require the results.

### 11.2 Backend Selection (Runtime, No Preprocessor)

The application render graph chooses a backend at runtime based on:

- User config:
  - `RendererSettings.reflection_capture_backend = Raster | DXR | Auto`
- Hardware capability:
  - DXR supported + enabled

There must be **no compile-time preprocessor gating** for backend selection.
Both backends are built and registered; the DXR backend reports unavailable at runtime if the device cannot support it.

The graph can compose either:

- `ProbeCaptureRasterPass` + `IblFilterPass`
- `ProbeCaptureDxrPass` + `IblFilterPass`

The filter pass is shared.

### 11.3 Suggested Coroutine Pattern

Illustrative (pseudo) coroutine sketch showing explicit integration points:

```cpp
co::Co<> MyRenderGraph::Run(graphics::CommandRecorder& recorder) {
  // ... depth prepass, etc

  // 1) Probe placement/generation can happen before rendering.
  //    (artist probes are already in the scene; grid strategy may add probes)
  placement_strategy_->GenerateProbes(*scene);

  // 2) Decide probe capture backend at runtime.
  auto& registry = renderer.GetProbeCaptureBackendRegistry();
  IProbeCaptureBackend& backend = registry.SelectBest(settings, device_caps);

  // 3) Schedule + execute a bounded amount of probe work.
  auto& probes = renderer.GetProbeSystem();
  probes.CollectFromScene(*scene);
  const auto work = probes.ScheduleWork(settings.probe_budget);
  co_await probes.CaptureAndFilter(recorder, backend, work);

  // 4) Now normal shading can sample probe IBL.
  co_await shader_pass.Execute(recorder);
}
```

### 11.4 Pluggable Backend Interface

Define a backend interface consumed by the renderer and/or capture subgraph.

```cpp
struct ProbeCaptureRequest {
  ProbeId probe;
  uint32_t face_size;
  bool include_sky;
  bool include_opaque;
};

struct ProbeCaptureOutputs {
  ShaderVisibleIndex source_cubemap_srv;
  // Optionally: UAV slots if backend writes directly to UAV cubemap
};

class IProbeCaptureBackend {
public:
  virtual ~IProbeCaptureBackend() = default;
  virtual const char* Name() const noexcept = 0;
  virtual bool IsAvailable(const RenderDeviceCaps&) const noexcept = 0;
  virtual co::Co<ProbeCaptureOutputs> Capture(
    graphics::CommandRecorder&, const ProbeCaptureRequest&) = 0;
};
```

Backends are wired into the RenderGraph via pass nodes that own the backend implementation.

Implementation note: rather than “pass nodes”, in Oxygen’s model this is often a **service object** owned by the renderer or app module, invoked from the render graph coroutine.

---

## 12. Capture Backends

### 12.1 Raster Probe Capture (Baseline — Implemented First)

**Concept**: Render the scene 6 times from the probe position to a cubemap render target.

Recommended capture pipeline per probe:

- `DepthPrePass` (probe view)
- `LightCullingPass` (probe view)
- `ShaderPass` (probe view; opaque only)
- Optional: `SkyPass` if sky is included

Practical constraints:

- This requires the renderer to be able to run a “mini frame” with a custom camera and custom framebuffer.
- Current pass registry is compile-time typed; the probe subgraph should use a separate `RenderContext` instance and must not pollute the main frame pass registry.

Implementation strategy:

- Add a lightweight `ProbeRenderContext` that reuses the existing passes but:
  - uses its own pass registry storage
  - uses a special `PreparedSceneFrame` for the probe camera
  - binds the same scene constants buffer

Output:

- A `TextureCube` (or `Texture2DArray` with 6 slices) that contains the captured environment.
- An SRV bindless slot for that texture.

### 12.2 DXR Probe Capture (Future — Infrastructure-Ready)

**Concept**: Ray trace the environment from the probe position into a cubemap.

Advantages:

- Captures full environment (not limited by camera frustum).
- Correct for dynamic scenes.
- Avoids SSR blind spots and reduces probe parallax artifacts.

Core requirements (runtime):

- DXR device support (queried via graphics device capabilities)
- Acceleration structures (BLAS/TLAS)
- Ray tracing pipeline state

Implementation outline:

- Build/maintain BLAS per mesh (mesh identity from ScenePrep/GeometryUploader is a good key).
- Build TLAS per frame from visible + relevant instances.
- Dispatch rays to write into a UAV cubemap:
  - `RWTexture2DArray<float4> outCube; // 6 slices`
  - Use a per-dispatch constant selecting face index and camera basis.

Ray tracing shading for capture:

- Minimal: shaded color = base PBR evaluation at hit (direct + emissive) with optional sky miss shader.
- For first milestone, do **one-bounce** specular not needed; we capture radiance for IBL.

Miss shader:

- Sample sky model (SkySphere cubemap or procedural atmosphere) to fill rays that miss geometry.

Output:

- A source cubemap SRV bindless slot for the captured radiance.

---

## 13. Filtering (IBL Convolution)

Filtering produces:

- Diffuse **irradiance** cubemap
- Specular **prefilter** cubemap (mipped)

The existing `IblComputePass` already performs filtering from a source cubemap SRV to target UAV slots.
To support probes, generalize the ownership of target textures.

### 13.1 Multi-Probe IBL Resource Manager

Introduce `ReflectionProbeIblManager` (renderer-owned) responsible for:

- Allocating per-probe textures:
  - `source_cubemap` (optional if backend writes to SRV-only texture)
  - `irradiance_map`
  - `prefilter_map`
- Creating SRV + UAV descriptors
- Providing bindless indices to shaders
- Managing lifetime and caching

Key design choice:

- Allocate per probe (simpler descriptor model), or
- Allocate as arrays/atlases (more complex but potentially more efficient)

Recommendation for MVP:

- Per-probe resources with SRV slots, to avoid shader-side cube-index indirection.

### 13.2 Updating `IblComputePass`

Update `IblComputePass` to operate on **a list of work items** per frame:

- For each dirty probe:
  - `source_cubemap_slot`
  - `irradiance_target_uav_slot`
  - `prefilter_target_uav_slot[mip]`

Important: the current `kMaxDispatches` (16) caps the number of dispatch constants.
For probes, we must either:

- Increase the constant buffer capacity (recommended), or
- Execute filtering in chunks across frames

Recommendation:

- Make the pass constants buffer dynamically sized (ring-buffer or per-frame upload buffer).

---

## 14. Update / Dirtying / Caching

### 14.1 Dirty Conditions

A probe is dirty when:

- Probe transform changes
- Probe settings change (resolution, include mask)
- Scene changes (dynamic objects) AND update policy is not static
- Sky changes (SkySphere cubemap changes, atmosphere LUT changes)

For MVP:

- Treat probes as dirty only when explicitly requested or when their settings change.

### 14.2 Budgeting

Capturing and filtering probes is expensive.
Introduce a per-frame budget:

- `max_probe_captures_per_frame`
- `max_probe_filter_mips_per_frame`

The registry schedules work across frames.

---

## 15. Debugging + Visualization

Add debug views to validate correctness:

- Visualize chosen probe index / weight
- Visualize raw probe cubemap sampling
- Visualize prefilter vs raw
- Visualize box-projected direction

Also add CPU logging of bindless slots for:

- `source_cubemap_slot`
- `irradiance_map_slot`
- `prefilter_map_slot`

---

## 16. File Touch Points

The following files/modules are expected touch points:

| Area | Files |
|------|-------|
| Scene components | `src/Oxygen/Scene/Components/Probe.h`, `ReflectionProbe.h`, `LightProbe.h` (new) |
| Renderer probe system | `src/Oxygen/Renderer/Probes/ProbeSystem.h` (new), `ReflectionProbeManager.h` (new) |
| Renderer IBL manager | `src/Oxygen/Renderer/Probes/ReflectionProbeIblManager.h` (new) |
| Environment data | `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.*`, `Types/EnvironmentStaticData.h` |
| GPU payload | `src/Oxygen/Renderer/Types/ProbeGpu.h` (new) |
| Capture backends | `src/Oxygen/Renderer/Probes/ProbeCaptureRasterBackend.h` (new) |
| Parallax strategies | `src/Oxygen/Renderer/Probes/Parallax/BoxProjection.h` (new) |
| IBL filtering | `src/Oxygen/Renderer/Passes/IblComputePass.*` (extend) |
| Shaders | `Shaders/Include/ProbeHelpers.hlsli` (new), `ForwardMesh_PS.hlsl` (extend) |

---

## 17. Implementation Plan (Concrete Steps)

This is the recommended incremental path with clear "done" points.

### Milestone A — Probe Authoring + Data Plumbing (No Capture Yet)

| ID | Task | Status |
|----|------|--------|
| A1 | Add `Probe` base + derived `ReflectionProbe` / `LightProbe` components to scene module | ⬜ Not started |
| A2 | Add `ReflectionProbeManager` (renderer-side registry): collects probes, assigns `ProbeId`, builds CPU probe list | ⬜ Not started |
| A3 | Extend `EnvironmentStaticDataManager` to allocate/upload probe list structured buffer | ⬜ Not started |
| A4 | Publish `bindless_reflection_probes_slot` and `reflection_probe_count` in `EnvironmentStaticData` | ⬜ Not started |
| A5 | Add placement strategy integration points: `ArtistPlaced` (no-op), `Grid3D` generator | ⬜ Not started |
| A6 | Add parallax strategy plumbing (shader-facing): `kNone`, `kBoxProjected` (initial) | ⬜ Not started |
| A7 | Add `ProbeHelpers.hlsli` with probe selection + parallax dispatch | ⬜ Not started |
| A8 | Update `ForwardMesh_PS.hlsl` to sample from probe IBL (dummy slots OK) | ⬜ Not started |

**Exit criteria**: Forward shader can pick a probe and sample dummy slots safely.

---

### Milestone B — Raster Probe Capture (Single Probe)

| ID | Task | Status |
|----|------|--------|
| B1 | Implement `ProbeCaptureRasterBackend` (render 6 faces into cubemap RT) | ⬜ Not started |
| B2 | Start with opaque-only + sky capture | ⬜ Not started |
| B3 | Implement `ReflectionProbeIblManager` to allocate per-probe textures and SRV slots | ⬜ Not started |
| B4 | Generalize `IblComputePass` to filter a provided target (per probe) | ⬜ Not started |
| B5 | Wire capture + filter into app render graph coroutine | ⬜ Not started |
| B6 | Verify reflection: sphere over cube reflects the cube | ⬜ Not started |

**Exit criteria**: A sphere over a cube reflects the cube using the probe cubemap.

---

### Milestone C — Multi-Probe + Blending

| ID | Task | Status |
|----|------|--------|
| C1 | Support N probes with scheduling and per-frame budgeting | ⬜ Not started |
| C2 | Implement box projection parallax correction in shader | ⬜ Not started |
| C3 | Add depth cubemap slot + SDF slot plumbing (infrastructure only) | ⬜ Not started |
| C4 | Implement probe blending (up to 2 probes with weights) | ⬜ Not started |
| C5 | Add debug visualization (probe index, raw cubemap, box-projected direction) | ⬜ Not started |

**Exit criteria**: Stable transitions when moving through probe volumes.

---

## 18. Notes on Correctness vs Expectations

- Probes are the mainstream baseline because they provide **complete** environment information and are stable.
- DXR improves quality and correctness for dynamic scenes but requires significant infrastructure. It is not planned at this stage.
- Box projection reduces parallax artifacts but is not perfect.
- This IBL design is compatible with a future SSR pass as an optional post effect, and a future DXR infrastructure, but the core lighting remains probe-driven.
