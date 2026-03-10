# Renderer Module Implementation Plan

Living roadmap for achieving feature completeness of the Oxygen Renderer.

**Last Updated**: March 10, 2026

Cross‑References: [bindless_conventions.md](bindless_conventions.md) |
[scene_prep.md](scene_prep.md) | [shader-system.md](shader-system.md) |
[passes/design-overview.md](passes/design-overview.md) |
[lighting_overview.md](lighting_overview.md) | [shadows.md](shadows.md) |
[virtual_shadow_map_backend.md](virtual_shadow_map_backend.md) |
[override_slots.md](override_slots.md)

Legend: `[ ]` pending | `[~]` in progress | `[x]` done

---

## Current State (Baseline)

Renderer core is functional with a minimal Forward+ foundation:

- **Bindless architecture**: SM 6.6 `ResourceDescriptorHeap[]` indexing, stable
  root signature (t0 unbounded SRV, s0 sampler table, b1 ViewConstants CBV,
  b2 root constants) with system-owned view data routed through
  `ViewConstants.bindless_view_frame_bindings_slot`
- **ScenePrep pipeline**: Two-phase Collection → Finalization with CPU frustum
  culling and per-view `PreparedSceneFrame`
- **Render passes**: `DepthPrePass`, `ShaderPass`, `TransparentPass` with 4 PSO
  variants each (opaque/masked × single/double-sided)
- **Shader permutations**: `MaterialPermutations.h` defines standard permutation
  names (`HAS_EMISSIVE`, `ALPHA_TEST`, `DOUBLE_SIDED`). Passes build
  `ShaderRequest` with material-derived defines. PSO cached by full request hash.
- **Lighting**: `LightManager` extracts scene lights; GPU buffers uploaded via
  transient buffers; shaders loop all lights per pixel (**no culling**)
- **Materials**: Runtime `MaterialShadingConstants` with bindless texture slots
  (base color, normal, metallic, roughness, AO, emissive). GGX specular +
  Lambert diffuse + emissive. Alpha test, UV transform, ORM packed detection.
  **PAK has full PBR**: clearcoat, transmission, sheen, specular, IOR —
  `MaterialLoader` reads all but `MaterialAsset` only exposes basic + emissive.
- **GPU instancing**: `RenderItemData` grouped by (Geometry, Material, LOD).
  `DrawMetadata.instance_count > 1` for batched draws. `InstanceDataBuffer` SRV
  with per-instance transform indices. Shaders use `SV_InstanceID`.
- **Geometry**: PAK supports LOD, AABB per geometry/mesh/submesh, `MeshType`
  enum. `GeometryAsset` stores LOD meshes; `GeometryUploader` interns buffers.
  `Vertex`: position, normal, texcoord, tangent, bitangent, color.
  **Gaps**: No bone weights (skinned), no morph targets, no hierarchical culling.

---

## Phase 1 – Tile/Clustered Light Culling [~]

Enable Forward+ light culling so pixels evaluate only relevant lights.
Supports both tile-based (2D) and clustered (3D) configurations.

### 1.1 Cluster Infrastructure [x]

- [x] Define `ClusterConfig` struct: grid dimensions, tile size, near/far,
      Z-binning scale/bias, presets for TileBased() and Clustered()
      → `src/Oxygen/Renderer/Types/ClusterConfig.h`
- [x] Establish lighting-owned cluster config publication through
      `LightingFrameBindings.light_culling`
- [x] Create cluster grid and light index list as transient structured buffers
      → Managed by `LightCullingPass` using `TransientStructuredBuffer`

### 1.2 Light Culling Compute Pass [x]

- [x] Create `LightCullingPass` class in `src/Oxygen/Renderer/Passes/`
      → `LightCullingPass.h`, `LightCullingPass.cpp`
- [x] Update `LightCulling.hlsl` compute shader for both tile and clustered:
  - Uses `PositionalLightData` directly (not legacy `GPULight`)
  - Computes cluster frustum, intersects light spheres
  - Writes `uint2(offset, count)` to cluster grid
  - Writes packed light indices to light index list
  - Supports 3D dispatch for clustered (groupID.z = depth slice)
- [x] Dispatch after DepthPrePass, before ShaderPass
- [x] Add `CLUSTERED` permutation to `EngineShaderCatalog.h`:
  - `CLUSTERED=0`: Tile-based mode (2D grid, per-tile depth bounds)
  - `CLUSTERED=1`: Clustered mode (3D grid, logarithmic Z-slices)
  - Compile-time `#if CLUSTERED` branching eliminates runtime checks

### 1.3 ShaderPass Integration [x]

- [x] Create `ClusterLookup.hlsli` with helper functions:
  - `ComputeClusterIndex()` for tile and clustered modes
  - `GetClusterLightInfo()`, `GetClusterLightIndex()`
  - `CLUSTER_LIGHT_LOOP_BEGIN/END` macros
- [x] Add `AccumulatePositionalLightsClustered()` to `ForwardDirectLighting.hlsli`:
  - Computes cluster index from screen position + linear depth
  - Falls back to brute-force loop if cluster buffers unavailable
  - Iterates only lights in the cluster

### 1.4 Renderer Integration [~]

- [x] Instantiate `LightCullingPass` in Examples' `RenderGraph`:
  - Constructor takes only `Graphics*` and `Config` (no staging dependencies)
  - Upload services obtained from `RenderContext.GetRenderer()` during execution
  - Called after DepthPrePass in render graph coroutine
  - PSO selection: use `CLUSTERED=0` or `CLUSTERED=1` based on config
- [x] Populate lighting-owned cluster bindings:
  - `Renderer` publishes `LightingFrameBindings` per view
  - `LightingFrameBindings.light_culling` receives cluster slots from
    `LightCullingPass`
  - cluster config fields are carried in the lighting-owned contract
- [x] Update `ForwardMesh.hlsl` to call `AccumulatePositionalLightsClustered()`:
  - Computes linear depth from world position and camera
  - Uses SV_POSITION for screen coordinates
  - Falls back to brute-force when cluster buffers unavailable
- [ ] Read scene root attachment for per-scene cluster config:
  - Query `scene.GetOverrideAttachments().Get(root_id, kRendering)`
  - Properties: `rndr_cluster_mode`, `rndr_cluster_depth`, `rndr_cluster_tile_px`
  - Default = tile-based; attachment presence triggers clustered mode
  - **TODO**: Implement after Override Attachments (Phase 3) is complete

### 1.5 Environment Systems Integration [~]

- [x] GPU plumbing: `EnvironmentStaticDataManager` builds bindless SRV from
  `SceneEnvironment`, resolves cubemap `ResourceKey` via `TextureBinder`,
  uploads per-frame slots, and now routes the result through
  `EnvironmentFrameBindings` / `ViewFrameBindings`.
- [x] Baseline render hooks: `SkyPass` fullscreen triangle after ShaderPass (depth-read, no clear); `SkySphere_PS/VS` consume `EnvironmentStaticData`; `ForwardMesh_PS` applies analytic fog via `AtmosphereHelpers.hlsli` with fallback sun dir when no directional light is bound.
- [x] SkyLight IBL in forward shading: sample sky cubemap for diffuse (lowest mip) and roughness-mapped specular, apply tint/intensity/diffuse/specular gains, add exposure from `ViewColorData`.
- [x] Sky exposure: `SkySphere_PS` multiplies sky color by exposure.
- [x] Specular IBL BRDF approx: apply split-sum approximation (no LUT) to specular IBL in `ForwardMesh_PS` and metalness-masked diffuse IBL.
- [~] SkyAtmosphere real implementation (end-to-end):

  **Phase A – LUT Infrastructure**

  1) [x] **Create `SkyAtmosphereLutManager` class**
     - Location: `src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h/.cpp`
     - Owns persistent textures: transmittance LUT (256×64, RG16F) and
       sky-view LUT (192×108, RGBA16F)
     - Tracks dirty state via hash of atmosphere parameters
     - Publishes `ShaderVisibleIndex` for each LUT via bindless heap
     - Interface: `GetTransmittanceLutSlot()`, `GetSkyViewLutSlot()`,
       `GetTransmittanceLutSize()`, `GetSkyViewLutSize()`, `IsDirty()`,
       `MarkClean()`, `UpdateParameters(const GpuSkyAtmosphereParams&)`
     - Follows `BrdfLutManager` pattern for resource lifecycle

  2) [x] **Create transmittance LUT compute shader**
     - Location: `Shaders/Atmosphere/TransmittanceLut_CS.hlsl`
     - Thread group: 8×8×1, dispatch covers 256×64 texture
     - Implements optical depth integration along view ray
     - Output: RG16F (Rayleigh + Mie extinction)
     - Reads atmosphere params from `EnvironmentStaticData`
     - Uses standard transmittance UV parameterization:
       `u = (cos_zenith + 0.15) / 1.15`, `v = sqrt(altitude / atmo_height)`

  3) [x] **Create sky-view LUT compute shader**
     - Location: `Shaders/Atmosphere/SkyViewLut_CS.hlsl`
     - Thread group: 8×8×1, dispatch covers 192×108 texture
     - Performs single-scattering raymarch with transmittance LUT sampling
     - Output: RGBA16F (inscattered radiance RGB, transmittance A)
     - UV parameterization: `u = azimuth / 2π`, `v = (cos_zenith + 1) / 2`
     - Requires sun direction from `LightingFrameBindings.sun`

  4) [x] **Create `SkyAtmosphereLutComputePass`**
     - Location: `src/Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h/.cpp`
     - Derives from `ComputeRenderPass`
     - Config: pointer to `SkyAtmosphereLutManager`
     - On `Execute()`: dispatch transmittance CS, barrier, dispatch sky-view CS
     - Only executes when `manager.IsDirty()` returns true
     - Calls `manager.MarkClean()` after successful execution

  **Phase B – Integration**

  1) [x] **Wire LUT manager into `EnvironmentStaticDataManager`**
     - Add `observer_ptr<SkyAtmosphereLutManager>` to constructor
     - In `BuildFromSceneEnvironment()`, populate:
       - `next.atmosphere.transmittance_lut_slot = lut_mgr->GetTransmittanceLutSlot()`
       - `next.atmosphere.sky_view_lut_slot = lut_mgr->GetSkyViewLutSlot()`
       - `next.atmosphere.transmittance_lut_width/height`
       - `next.atmosphere.sky_view_lut_width/height`
     - Call `lut_mgr->UpdateParameters(next.atmosphere)` to trigger dirty check

  2) [x] **Update `SkySphere_PS.hlsl` with LUT sampling**
     - Create `Shaders/Renderer/SkyAtmosphereSampling.hlsli`:
       - `SampleTransmittanceLut(float cos_zenith, float altitude)`
       - `SampleSkyViewLut(float3 view_dir, float3 sun_dir, float altitude)`
       - `ComputeSunDisk(float3 view_dir, float3 sun_dir, float angular_radius)`
     - In `SkySphere_PS.hlsl` when `env_data.atmosphere.enabled`:
       - Check if `transmittance_lut_slot != K_INVALID_BINDLESS_INDEX`
       - If valid: sample sky-view LUT + sun disk
       - If invalid: keep current gradient fallback
     - Apply exposure from `ViewColorData`

  3) [x] **Add RenderGraph integration**
     - In `Examples/Common/RenderGraph.h/.cpp`:
       - Add `sky_atmosphere_lut_pass_` and `sky_atmosphere_lut_pass_config_`
       - Create pass in `SetupRenderPasses()` (requires `Graphics*`)
     - In `RunPasses()`: execute before `SkyPass` (but can run early in frame)
     - Pass only dispatches when LUT manager reports dirty
     - For production: move to `Renderer` class internal pass list

  **Phase C – Aerial Perspective**

  1) [x] **Add aerial perspective to `ForwardMesh_PS`**
     - Location: `Shaders/Renderer/AerialPerspective.hlsli`
     - `ShouldUseLutAerialPerspective(atmo)` checks LUT validity and flags
     - `ComputeAerialPerspective(env_data, world_pos, camera_pos, sun_dir)`:
       - Samples transmittance LUT for optical depth along view ray
       - Approximates inscatter from sky-view LUT scaled by opacity
       - Returns `AerialPerspectiveResult{inscatter, transmittance}`
     - `ApplyAerialPerspective(color, ap)` blends result onto lit color
     - In `ForwardMesh_PS.hlsl` after lighting accumulation:
       - Checks `ATMOSPHERE_USE_LUT` flag via `ShouldUseLutAerialPerspective()`
       - If enabled: applies LUT-based aerial perspective
       - If disabled or LUT invalid: falls back to existing analytic fog
     - C++ `AtmosphereFlags` enum mirrors HLSL constants
     - `Renderer::PrepareView()` sets `kUseLut` when LUTs are generated

  **Phase D – Validation & Debug**

  1) [ ] **Add ImGui debug overlay**
     - In example ImGui code (e.g., `MainModule.cpp`):
       - Show LUT validity, resolution, dirty state
       - Show sun direction/illuminance from `LightingFrameBindings.sun`
       - Add checkbox to force analytic fallback (sets `atmosphere_flags` bit)
       - Add checkbox to visualize LUT (render LUT as overlay quad)
     - Add RenderDoc markers in compute pass for easy capture

  - [~] Bullet-proof designated sun pipeline:
    - [x] Select canonical sun on CPU: prefer `DirectionalLight::IsSunLight()` flagged light; if multiple, pick highest intensity; fallback to first directional if none flagged.
    - [x] Surface sun direction/intensity into environment data: add fields to **dynamic** payload (C++/HLSL) for sun direction (toward sun) and luminance scale, and fill them per view during ScenePrep/renderer update.
    - [x] Update `LightManager` outputs to publish the chosen sun (index or baked direction) so ScenePrep can populate environment dynamic data without shader-side heuristics.
    - [x] Consume the authoritative sun in shaders: remove “first directional light” fallback in `ForwardMesh_PS`/sky/fog paths and read the dynamic payload sun fields; keep a final hardcoded direction only if no sun is available.
    - [ ] Add validation hooks: debug overlay/ImGui showing the selected sun id, direction, and intensity, and a renderdoc-friendly marker to confirm binding.
- [x] SkyLight capture path: support `kCapturedScene` by rendering sky/background into cubemap, convolving diffuse and specular prefilter, caching BRDF LUT; fall back to `kInvalidDescriptorSlot` when unavailable.
- [x] BRDF LUT asset/binding: generate BRDF integration LUT at runtime
  (RG16F, Hammersley GGX) via `BrdfLutManager`, bind slot into
  `sky_light.brdf_lut_slot` through `EnvironmentStaticDataManager`; shaders
  sample when valid, fallback to analytic approx otherwise. LUT upload is
  async; while pending, GetOrCreateLut returns an invalid slot so consumers
  must keep the analytic fallback path.
- [ ] Volumetric clouds: consume `GpuVolumetricCloudParams` to raymarch low-res cloud color/transmittance (temporal reprojection), composite into sky before transparents, optional shadow mask for sun light.
- [x] Post-process pass foundation: `GpuPostProcessParams`, `AutoExposurePass`, and `ToneMapPass` exist in the renderer/pipeline.
- [x] PostProcessVolume authoring path: `DemoShell` creates/binds `SceneEnvironment` + `PostProcessVolume`, persists authored exposure settings, and applies them to both the active scene and pipeline runtime.
- [ ] PostProcessVolume renderer runtime:
  - [ ] Remove the `EnvironmentStaticDataManager` testing bypass so scene-authored `PostProcessVolume` data actually populates `EnvironmentStaticData.post_process` at runtime.
  - [ ] Make renderer-owned post-process config per view derive from the scene-authored `PostProcessVolume` contract instead of relying on `DemoShell` pipeline setters as the only live path for exposure/tonemap/auto-exposure settings.
  - [ ] Keep the exposure runtime contract explicit and code-accurate: use the prepared manual exposure seed plus `AutoExposurePass` output consumed by `ToneMapPass`; do not rely on a nonexistent `EnvironmentDynamicData.exposure` field.
  - [ ] Wire authored bloom/compositing parameters into real renderer behavior: consume `bloom_intensity`, `bloom_threshold`, `saturation`, `contrast`, and `vignette_intensity` in passes/shaders instead of only storing them in `GpuPostProcessParams`.
- [x] Tooling + demos: sample apps hydrate environment systems through `DemoShell` (`SceneEnvironment` creation + SkySphere/SkyLight/PostProcessVolume defaults) and expose validation toggles for sky/fog/exposure paths.
- [ ] Tooling + demos: ensure cubemap assets packaged for validation flows and expose cubemap/bindless slots in ImGui debug.

---

## Phase 2 – Light Channel Masks

Intrinsic property for selective light-object interaction (Layer 1 extension).

### 2.1 Component Extension

- [ ] Add `uint8_t light_channel_mask_` to `RenderableComponent` (default 0xFF)
- [ ] Add `uint8_t light_channel_mask_` to `LightComponent` base (default 0xFF)
- [ ] Add getters/setters: `GetLightChannelMask()`, `SetLightChannelMask()`

### 2.2 ScenePrep Integration

- [ ] Include `light_channel_mask` in `RenderItemData` during extraction
- [ ] Include `light_channel_mask` in `GpuLightData` during upload
- [ ] Pack into existing `DrawMetadata` (8 bits available)

### 2.3 Shader Integration

- [ ] Add `light_channel_mask` to `DrawMetadata.hlsli`
- [ ] Add `channel_mask` to `GpuLight` struct
- [ ] Update lighting loop: `if ((draw.light_mask & light.channel_mask) == 0) continue;`

### 2.4 PAK Format

- [ ] Add `light_channel_mask` field to `LightDesc` in PAK format
- [ ] Update `LightLoader` to populate mask from PAK

---

## Phase 3 – Override Attachments

Sparse, domain-tagged property bags for per-node customization (Layer 3).

### 3.1 Core Data Structures

- [ ] Create `OverrideAttachment.h` in `src/Oxygen/Scene/`:
  - `enum class OverrideDomain : uint8_t` (kRendering, kStreaming, kPhysics,
    kAudio, kGameplay, kEditor)
  - `using PropertyValue = std::variant<bool, int32_t, uint32_t, float,
    std::string, glm::vec2, glm::vec4, AssetKey>`
  - `struct OverrideAttachment` with domain, inheritable flag, property map
  - Template accessors: `Get<T>()`, `GetOr<T>()`

### 3.2 Scene-Level Storage

- [ ] Create `OverrideAttachmentStore` class:
  - Sparse map: `(NodeId, OverrideDomain) → OverrideAttachment`
  - `Attach()`, `Get()`, `GetEffective()` (inheritance walk-up), `AllInDomain()`
- [ ] Add `OverrideAttachmentStore override_attachments_` to `Scene`
- [ ] Add `GetOverrideAttachments()` accessor to `Scene`

### 3.3 ScenePrep Consumption

- [ ] Query `OverrideDomain::kRendering` attachments during node processing
- [ ] Handle `rndr_graph_id`: pass to game module for render graph selection
- [ ] Handle `rndr_shader_on/off`: apply to shader feature mask
- [ ] Handle `rndr_pass_id`: register node for custom pass

### 3.4 PAK Serialization

- [ ] Define `OVRD` chunk format: attachment_count, per-attachment data
- [ ] Implement `OverrideAttachmentLoader` to populate `OverrideAttachmentStore`
- [ ] Update PAK writer to emit `OVRD` chunk from editor data

---

## Phase 4 – Material Instances

Lightweight parameter variations of parent materials (Layer 4).

### 4.1 Parameter Schema

- [ ] Add `struct ParamSchema` to `Material`: param_id, type, instance_slot,
      default_value
- [ ] Populate schema from shader reflection or material definition
- [ ] `instance_slot` maps to InstanceData float4 (0-3), 0xFF = not instanceable

### 4.2 Asset Format

- [ ] Define `MaterialInstanceAssetDesc` in PAK format:
  - `parent_key`, `flags` (has_texture_overrides), `param_count`, `texture_count`
  - `ParamOverride[]`: param_id, value[4]
  - `TextureOverride[]`: slot, texture_key
- [ ] Create `MaterialInstanceLoader` to load from PAK

### 4.3 Runtime Representation

- [ ] Create `MaterialInstance` class:
  - `GetParent()`, `HasTextureOverrides()`, `GetParam()`, `GetTexture()`
  - `ComputeInstanceDeltas() → InstanceParams` for GPU instancing
  - `GetBatchKey() → uint64_t` for instancing grouping

### 4.4 RenderableComponent Integration

- [ ] Update `SetMaterialOverride(lod, submesh, asset)` to accept `MaterialInstance`
- [ ] Renderable stores reference; doesn't distinguish MI from base material

### 4.5 ScenePrep Batching

- [ ] Group by `MaterialInstance::GetBatchKey()` instead of raw material handle
- [ ] Compute and pack instance deltas into `InstanceData` buffer

---

## Phase 5 – Extended GPU Instancing (64-byte Params)

Extend per-instance data to support material instance deltas (Layer 5).

### 5.1 InstanceData Buffer Extension

- [ ] Extend `InstanceData` struct to 64 bytes:
  - `uint32_t transform_index` (4 bytes)
  - `uint32_t padding` (4 bytes)
  - `float4 tint` (16 bytes)
  - `float4 param_mult` (16 bytes) — roughness, metallic, ao, emission
  - `float4 uv_transform` (16 bytes) — scale.xy, offset.xy
  - `float4 custom` (8 bytes) — game-specific
- [ ] Update `InstanceDataBuffer` allocation to 64-byte stride

### 5.2 ScenePrep Packing

- [ ] Call `MaterialInstance::ComputeInstanceDeltas()` during finalization
- [ ] Pack deltas into `InstanceData.tint`, `param_mult`, `uv_transform`
- [ ] Zero-initialize for base materials (no delta)

### 5.3 Shader Integration

- [ ] Update `InstanceData.hlsli` to match new layout
- [ ] Apply tint: `base_color *= inst.tint`
- [ ] Apply param multipliers: `roughness *= inst.param_mult.x`, etc.
- [ ] Apply UV transform: `uv = uv * inst.uv_transform.xy + inst.uv_transform.zw`

---

## Phase 6 – Post-Process Pass & Bloom

Add fullscreen post-processing with bloom to make emissive materials shine.

### 6.1 PostProcessPass Implementation

- [x] Create tone-mapping pass and shader: `ToneMapPass` +
      `Compositing/ToneMap_PS.hlsl`
- [x] Create auto-exposure compute path: `AutoExposurePass` +
      `Compositing/AutoExposure_Histogram_CS.hlsl` and
      `Compositing/AutoExposure_Average_CS.hlsl`
- [ ] Create unified `PostProcessPass` class in `src/Oxygen/Renderer/Passes/`

### 6.2 Bloom Effect

- [ ] Implement brightness threshold extraction pass
- [ ] Implement separable Gaussian blur (downsample → blur → upsample chain)
- [ ] Composite bloom with tone-mapped color
- [x] Expose bloom intensity/threshold in `GpuPostProcessParams`

---

## Phase 7 – Transparent Sorting

Enable correct alpha blending with back-to-front ordering.

### 7.1 Depth-Based Sort Key

- [x] Add view-relative transparent sort distance to `RenderItemData` during
      extraction (`sort_distance2`)
- [ ] Implement 64-bit sort key: `(depth << 32) | (material << 16) | mesh`

### 7.2 Transparent Partition Sorting

- [x] Sort transparent partition by descending camera distance
- [x] Update `PreparedSceneFrame` to expose sorted draw range

---

## Phase 8 – Shadow System

Design authority for all shadow architecture, renderer contracts, resource
layouts, and phased rollout ordering lives in [shadows.md](shadows.md). This
plan section tracks execution work only.

### 8.1 Shared Foundations

- [~] Add renderer-owned shadow runtime services per `shadows.md`
- [x] Add baseline shadow metadata/contracts and bindless plumbing:
      `DirectionalShadowMetadata`, per-light shadow indices/flags, and the first
      shadow-routing publication path through `ViewFrameBindings`
- [~] Generalize the baseline metadata into family-independent shadow-product
      records and implementation-selection plumbing
- [ ] Extend GPU shadow metadata/contracts and bindless slots to match the
      final shadow system design, including family-specific resources for
      conventional and virtual backends
- [x] Carry `cast_shadows` / `receive_shadows` through ScenePrep item data
- [x] Add shadow-caster draw classification and pass routing through ScenePrep,
      `PreparedSceneFrame`, and draw emission
- [ ] Add deterministic family-selection policy inputs (tier, capability,
      budget, debug override) without introducing scene-authored implementation
      flags
- [ ] Add renderer debug/telemetry hooks required by the shadow system design

Execution note, March 8, 2026:

- `ShadowManager` now exists as the renderer-owned shadow runtime entry point.
- `LightManager` no longer owns uploaded directional shadow metadata; it emits
  `DirectionalShadowCandidate` intent records.
- Shared shadow-product publication now uploads `ShadowInstanceMetadata` and
  directional payloads through `ShadowFrameBindings`.
- Synthetic sun shadowing now feeds `ShadowManager` directly when the effective
  sun is `SunSource::kSynthetic` and `Sun::CastsShadows()` is true. The
  resolved sun's shadow lookup is published through
  `ShadowFrameBindings.sun_shadow_index` instead of requiring a scene-backed
  directional light record. For the current single-source directional VSM
  slice, the synthetic sun owns that slot and scene sun-tagged directionals do
  not disqualify virtual publication. The canonical directional split defaults
  are now explicit and non-zero: `8 / 24 / 64 / 160` with
  `distribution_exponent = 1.0`.
- ScenePrep finalization now publishes explicit shadow-caster participation
  through `PassMask::kShadowCaster`, keeping shadow submission in the existing
  draw metadata and partitioning path.
- The current slice stops at metadata publication and validation. No
  directional shadow map allocation, rendering, or family-selection policy is
  marked complete yet.

### 8.2 Conventional Directional Path

- [x] Implement directional shadow resource allocation and lifecycle management
- [x] Implement cascaded directional shadow pass scheduling and rendering
- [x] Replace bootstrap cascade fitting with stable texel-snapped cascade
      coverage and tighter light-space depth fitting
- [x] Publish directional metadata needed for cascade blend bands and
      kernel-aware sampling
- [x] Integrate directional shadow evaluation into forward shading paths
- [x] Replace bootstrap manual depth taps with the normal comparison-sampling
      path and keep manual comparison taps only as an explicit compatibility
      fallback
- [x] Add renderer-controlled raster depth-bias policy for directional shadow
      passes
- [x] Finalize directional receiver-bias policy so authored bias, normal bias,
      and renderer bias cooperate instead of fighting each other
- [x] Add directional invalidation, cascade blending, and tier-policy behavior
      on the shared shadow-product contract

Execution note, March 9, 2026:

- The conventional directional CSM path is now complete for the current
  renderer shadow scope.
- The first hardening slice is now in code: conventional directional cascades
  use texel-snapped stable XY coverage based on a stable enclosing sphere
  rather than refitting orthographic XY bounds directly from per-frame light-
  space min/max corners.
- The second hardening slice is now in code: the forward shadow sampler blends
  across directional cascade boundaries instead of hard-switching at split
  distances, reducing visible receiver stripes on large surfaces.
- The third hardening slice is now in code: `ConventionalShadowRasterPass`
  owns an explicit raster depth-bias policy through the existing depth-pass
  PSO path.
- The fourth hardening slice is now in code: directional shadow metadata
  publishes per-cascade world-units-per-texel, and the forward shadow sampler
  derives a renderer-controlled receiver bias from that footprint so default
  directional/synthetic-sun shadows are not forced to rely on zero authored
  bias values.
- The fifth hardening slice is now in code: conventional directional shadows
  use a dedicated bindless comparison sampler as the normal path, with the old
  manual depth-tap PCF retained only as an explicit shader fallback.
- The sixth hardening slice is now in code: conventional directional cascades
  expand orthographic coverage by a small kernel-aware texel guard band and use
  white-border comparison sampling so edge taps remain deterministic instead of
  clamping into shadowed texels at the edge of the cascade.
- The seventh hardening slice is now in code: the renderer publishes current-
  view shadow-caster bounding spheres into `PreparedSceneFrame`, and
  `ShadowManager` uses them to tighten directional cascade depth range only
  for casters that overlap the cascade footprint instead of padding depth from
  receiver slice corners alone.
- The eighth hardening slice is now in code: the conventional comparison-
  sampled path no longer relies on the hardware 2x2 footprint alone; it uses
  an explicit 3x3 tent-weighted comparison PCF filter for noticeably less
  jagged directional shadow edges while retaining the manual depth-load path
  only as the explicit compatibility fallback.
- The ninth hardening slice is now in code: the comparison path uses a wider
  5x5 tent-weighted PCF footprint and the cascade guard band is expanded to
  match that kernel, specifically to move edge quality away from the current
  prototype-grade aliasing.
- The tenth hardening slice was an attempted directional utilization
  improvement that moved cascades to a tighter texel-snapped rectangular XY
  fit. That slice is no longer considered valid for production CSM quality
  because it made orthographic coverage breathe with camera angle in large
  scenes such as Sponza.
- The eleventh hardening slice is now in code: the non-authored directional
  split fallback no longer uses the earlier simple power curve. It uses a
  practical linear/logarithmic split blend, while preserving authored cascade
  distances when content provides them explicitly.
- The twelfth hardening slice partially landed the right filtering direction
  (narrower texel-aware blend bands and tighter near-cascade filtering), but
  one part of that slice was architecturally wrong for production stability:
  arbitrary "first valid cascade coverage" selection caused pixels to jump
  between cascades as camera angle changed.
- The thirteenth hardening slice is now in code: ScenePrep no longer makes
  main-view visibility the sole gateway for directional shadow submission.
  Offscreen shadow casters are emitted through the existing draw-metadata path
  as shadow-only records, main-view-visible draws are marked explicitly, and
  main color/depth/wireframe passes ignore shadow-only records while
  the conventional raster shadow pass continues to consume
  `kShadowCaster` partitions.
- The fourteenth hardening slice is now in code: the renderer owns an explicit
  shadow quality tier contract. Conventional directional resolution is no
  longer limited to authored `ShadowResolutionHint` alone; tier policy can now
  promote the active dominant directional shadow product to a higher runtime
  resolution when the shadow budget allows it.
- The fifteenth hardening slice is now in code: conventional directional
  cascades use a texel-snapped sphere-wrapped XY fit again, while keeping the
  later quality work on filtering, depth tightening, split policy, and tiered
  resolution. This restores the stability requirement that large scenes need
  when the camera changes distance or view angle.
- The sixteenth hardening slice is now in code: directional shading is back to
  stable interval-based cascade selection, with only local neighbor fallback
  when a projection is genuinely invalid and neighbor-only blend at cascade
  handoff. This removes view-angle-dependent cascade hopping from the earlier
  first-valid search while preserving continuity where adjacent cascades overlap.
- The seventeenth hardening slice originally disabled conventional raster
  slope-scaled bias to avoid double-bias detachment. That status was too
  aggressive and is now superseded.
- The eighteenth hardening slice is now in code: conventional directional
  shadows keep constant raster depth bias, add a small tightly clamped raster
  slope-scaled bias, and retain the receiver-side slope-aware
  normal/light-direction bias. This matches the same layered bias policy now
  proven necessary in the virtual path, without reopening detached contact
  shadows.
- Manual validation in `RenderScene` and `Sponza` was used to close the
  remaining production-quality gaps after the automated build/test coverage
  below.
- Validation evidence:
  - `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - user-performed visual validation in `RenderScene`
  - user-performed visual validation in `Sponza`

### 8.3 Multi-Technique Architecture Remediation

- [x] Split the current shadow published-view blob into separate contracts:
      shading-facing `ShadowFramePublication`, backend-facing raster render
      plan, and backend-private cache/residency state
- [x] Remove directional-only render-planning data from the shading
      publication path so passes do not depend on grab-all shadow state
- [x] Introduce explicit backend seams under `ShadowManager` for:
      conventional raster shadow planning/execution and virtual shadow
      planning/execution
- [x] Replace the directional-only conventional pass contract with a generic
      raster shadow render-plan contract that reuses `DepthPrePass`
      infrastructure without baking directional-cascade assumptions into the
      public pass interface
- [x] Move shader shadow dispatch to shared `ShadowInstanceMetadata` first,
      then resolve directional/spot/point and conventional/virtual payloads in
      shadow helpers rather than in lighting code
- [x] Replace `ViewId`-only published shadow caching with explicit
      shadow-input/backend-state invalidation keys
- [x] Add focused automated coverage for:
      - shadow-only casters surviving ScenePrep and reaching raster shadow jobs
      - main-view passes excluding shadow-only draws
      - generic shadow-product shader dispatch
      - invalidation when shadow inputs/backend state change

Execution note, March 9, 2026:

- Implemented in code.
- `ShadowManager` is now a coordinator over explicit backend seams rather than
  the owner of conventional directional rendering logic.
- `ConventionalShadowBackend` now owns conventional directional resource
  allocation, planning, metadata fill, and publication for the current path.
- Shading-facing publication and backend render planning are now separate:
  - `ShadowFramePublication`
  - `RasterShadowRenderPlan`
- `ConventionalShadowRasterPass` now consumes generic raster jobs rather than
  directional-only published state.
- Forward shading now dispatches from `ShadowInstanceMetadata` and resolves
  directional conventional payloads inside shadow helpers.
- Conventional shadow publication now invalidates from shadow-relevant hashed
  inputs rather than `ViewId` alone.
  - this now includes a prepared shadow-caster content hash derived from
    shadow-caster draw metadata plus world transforms, not only coarse caster
    bounds
- Focused automated coverage now exists for:
  - directional publication plus raster-plan publication
  - synthetic-sun publication
  - invalidation when shadow inputs change within a frame
  - shadow-only casters staying out of main-view partitions
- Validation evidence:
  - `cmake -S . -B out/build-vs`
  - `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Engine/oxygen-engine.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.DrawMetadataEmitter.Tests.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.DrawMetadataEmitter.Tests.exe`

### 8.4 Virtual Shadow-Map Foundations

- [x] Implement `VirtualShadowMapBackend` resource ownership and lifecycle
- [x] Add the first virtual page-pool allocation, page-table/indirection
      resources, and shading publication for directional products
- [ ] Implement sparse virtual shadow request generation, deterministic
      residency policy, and fallback bookkeeping
- [x] Extend shader-side sampling helpers to dispatch through the
      family-independent shadow-product metadata

Execution note, March 9, 2026:

- The concrete backend spec for this phase now lives in
  [virtual_shadow_map_backend.md](virtual_shadow_map_backend.md).
- The locked first implementation shape is:
  - directional virtual shadow maps first
  - light-space clipmaps
  - atlas-backed physical page pool
  - correctness-first residency for the current visual-validation slice:
    no frame-budgeted page selection in the active path
  - that slice activates only when the view resolves to exactly one active
    directional shadow product; multi-directional views remain conventional
  - virtual rasterization remains scheduled after main depth pre-pass so the
    later receiver-driven sparse request path does not move again
  - shared shading dispatch rooted in `ShadowInstanceMetadata`
- Live code now includes:
  - `VirtualShadowMapBackend`
  - `VirtualShadowPageRasterPass`
  - directional virtual metadata publication through `ShadowFrameBindings`
  - renderer policy plumbing for conventional vs virtual directional selection
  - correctness-first directional VSM configuration:
    - virtual clipmap density is fixed per quality tier
    - physical atlas capacity is fixed per quality tier
    - the active runtime path no longer applies frame-budget density caps or
      budget-driven backend fallback inside VSM
  - mandatory coarse clipmap backbone selection from the visible frustum
    footprint every frame; this is the only bootstrap path before compatible
    depth feedback arrives
  - feedback-driven fine refinement is the only steady-state request source;
    CPU receiver-bounds refinement is no longer part of the live algorithm
  - virtual address-space density is now fully decoupled from physical/update
    capacity:
    - the virtual clipmap grid stays dense and stable per quality tier
    - residency/update capacity is a separate physical-atlas limit
    - the active runtime no longer shrinks the virtual page grid to fit the
      current page budget, which had been producing giant unstable shadow wedges
  - directional VSM clipmaps now use a stable exponential ladder:
    - the base page world size is quantized to a power-of-two step
    - each clip level doubles page world size and coverage
    - the runtime no longer rebuilds a coarse six-level cascade-like ladder
      from view-dependent extents
  - directional VSM request generation remains footprint-driven:
    - request generation chooses the coarsest containing clip whose logical
      texel size still matches the receiver footprint
    - it requests that clip, optionally one finer clip near the threshold, and
      all containing coarser clips
  - directional VSM shading is not footprint-driven:
    - shading samples the finest available containing clip first
    - invalid pages fall back coherently to coarser clips
  - cross-frame resident-page reuse for snapped-identical virtual plans:
    unchanged page tables and clip metadata keep their resident pages, reuse
    physical page allocations, and skip redundant page rerasterization
  - resident-page reuse is now content-safe: light/caster input changes force
    rerasterization even when snapped clip metadata and page requests remain
    unchanged
  - VSM reuse invalidation now includes a prepared shadow-caster content hash
    so moving/rotating casters that keep similar coarse bounds do not leave
    stale virtual shadows behind
  - virtual pages now remain pending until the raster pass marks them rendered,
    preventing same-frame republish from clearing the only raster jobs before
    any virtual shadow content exists
  - publishing the final `ViewFrameBindings` slot now patches the pending
    virtual raster jobs consumed by the page-raster pass, not only the cached
    source job list
  - the virtual page raster pass now clears only the page rectangles being
    rerasterized instead of clearing the full physical atlas every frame, so
    resident-page reuse does not silently wipe valid shadow contents
  - released virtual pages return their physical tiles to the backend free
    list instead of pinning the initial working set forever
  - shader-side invalid-page fallback to coarser clip levels so partially
    resident virtual coverage remains continuous instead of dropping to fully
    lit when the finer clip page is absent
  - virtual comparison PCF clamped to page interiors so the first slice does
    not bleed across unrelated atlas pages
  - virtual pages now rasterize with page-local guard texels and sample only
    the logical page interior, so PCF can consume valid page-local border data
    instead of exposing visible seams at page boundaries
  - virtual bias/filter sizing now derives from the logical interior texel size
    after page guard reservation, not the full physical page resolution, so
    large flat receivers do not retain projected striping from under-biased
    virtual texels
  - virtual page ownership and the full in-page PCF footprint now stay on the
    un-biased receiver position; only the comparison depth follows the biased
    receiver so shadow bias cannot translate the sampled shadow laterally away
    from the caster base
  - virtual comparison-PCF now applies receiver-plane depth bias per tap so
    large-kernel filtering does not reuse one receiver depth for every sample
  - virtual comparison-PCF now resolves neighboring taps through the virtual
    page table instead of clamping the full kernel inside the center page, so
    page-row/column boundaries can be filtered across page ownership changes
  - virtual page rasterization now uses a clamped hardware slope-scaled depth
    bias instead of a zero-slope rasterizer state, so the virtual path no
    longer relies on receiver-side bias alone to fight regular self-shadow
    moire on broad planes
  - resident virtual pages now carry explicit residency state
    (`PendingRender` / `ResidentClean`) plus `last_touched_frame`, and the
    live introspection surface publishes resident/clean/pending page counts
    instead of treating residency as one opaque validity boolean
  - clean unrequested virtual pages now stay resident across publishes and are
    only evicted when a new request needs their physical tile; current
    eviction ordering is deterministic per view: unrequested pages only,
    coarser clip first, then oldest `last_touched_frame`, then stable page
    index
  - retained clean pages are now cache-only; they no longer stay mapped in the
    current frame page table, and introspection publishes `mapped_page_count`
    separately from total resident pages
  - virtual page-local sampling now flips `y` to match the rendered atlas tile
    orientation under D3D/Oxygen conventions
  - virtual physical page size is fixed from quality tier and physical atlas
    capacity, not frame budget
  - ultra-tier virtual shadows now use a larger fixed physical atlas and page
    size so fine virtual pages can exceed the old `256`-texel page cap
  - the virtual directional runtime now uses a denser address space than
    before, while keeping the physical pool separately budget-bounded:
    - ultra-tier virtual shadows use `6` clip levels instead of inheriting the
      conventional `4`-cascade shape
    - the physical page pool is no longer sized as if every virtual page had
      to be resident simultaneously
  - virtual clip selection now blends from a fine clip level into its next
    coarser neighbor near the outer clip boundary instead of hard-switching
    across nested clipmap regions
  - runtime now logs one backend-selection line when a view switches between
    virtual, conventional, or no directional shadow backend so VSM activation
    can be verified without per-frame spam
- Sparse receiver-driven request generation is partially in place, but full
  residency resolve, dirty-page tracking, and the final eviction policy remain
  explicitly pending after this slice.
- A runtime depth-driven request producer is now live:
  - `VirtualShadowRequestPass` runs after `DepthPrePass`
  - it writes a deduplicated request-bit mask on the GPU from main-depth
    receiver samples and current directional virtual metadata
  - it copies that bit mask into a per-frame-slot readback buffer
  - on safe slot reuse, the CPU decodes and submits per-view request
    feedback to `ShadowManager`
- The current request model therefore uses:
  - live depth-driven request feedback after the first safe frame-slot latency
  - compatible feedback as the only steady-state fine request source
  - a mandatory coarse clipmap backbone over the visible frustum footprint
    every frame
  - directional VSM no longer chooses the first containing clip in either
    shading or request generation; clip selection is now driven by projected
    light-space receiver footprint so far views do not force fine-clip page
    selection across the entire visible scene
  - feedback-driven requests now apply a bounded page guard band so fine
    virtual coverage does not disappear exactly at the camera frustum edge
  - feedback-driven requests now build stable per-clip requested regions
    instead of reacting directly to sparse per-page hits
  - oversubscribed feedback uses smaller guard dilation plus current mapped-page
    pinning, while non-oversubscribed feedback keeps the broader guard band for
    continuity
  - mapped-page hysteresis under request pressure, so currently mapped
    requested pages are pinned first
  - requested virtual coverage now resolves into stable contiguous per-clip
    feedback regions instead of sparse page-by-page winners
  - coarser backbone clips are mapped first, then fine refinement clips, so
    valid fallback coverage exists by construction when physical tile capacity
    is exhausted
- The first slice is still not default-safe for heavy scenes like Sponza.
  Requests are now bounded and cross-frame reusable, but the path still lacks
  the final GPU residency-resolve/update pass, dirty-page tracking, and full
  eviction model. Until that sparse residency path lands,
  demo/runtime validation must keep VSM opt-in rather than the default
  directional path. Budgeted VSM page/update policy is deferred until after
  correctness and quality are closed.
- Validation evidence for the first slice:
  - `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Engine/oxygen-engine.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- current focused coverage includes:
    - depth-feedback-driven virtual page selection
    - compatible request feedback reuse within a bounded freshness window
    - bounded bootstrap behavior before the first compatible feedback arrives
    - resident-page reuse for identical virtual plans
    - rerasterization when caster inputs change under identical snapped plans
  - the first parallel validation attempt hit the known MSBuild shared-PDB
    race in dependent projects; sequential reruns passed with no source
    changes
- Default demo policy was corrected back to conventional directional shadows.
  `RenderScene` now exposes VSM through `--directional-shadows=prefer-virtual`
  or `--directional-shadows=virtual-only`.
- Status remains `in_progress` until visual validation confirms the first VSM
  slice is rendering correctly.

### 8.5 Directional Virtual Shadow Path

- [~] Implement initial directional virtual coverage selection, bounded
      receiver-driven residency, and page rendering
- [~] Publish directional virtual addressing metadata and first-pass debug
      introspection
- [~] Integrate directional virtual shadow evaluation into forward lighting
      without a parallel lighting contract
- [~] Add sparse receiver-driven requests, deduplication, and residency
      updates
- [ ] Add directional virtual invalidation and debug tooling hardening

Directional VSM Milestone: `Directional VSM Production Candidate`

This is the next milestone worthy of being treated as a real delivery gate.
It is reached only when all of the following are true:

- request generation is feedback/depth-driven with no steady-state dependency
  on CPU visible-receiver fallback
- request deduplication and per-frame budgeted page scheduling are active end
  to end in the live residency path
- resident-page reuse, invalidation, and eviction are deterministic and
  content-safe under moving/rotating casters
- `virtual-only` is visually stable in `RenderScene` and Sponza under camera
  translation/rotation
- edge quality is brought to parity-targeting territory with the current
  conventional directional path, rather than remaining a visibly inferior
  baseline
- the required debug surfaces exist for:
  - requested pages
  - mapped/resident pages
  - coarse fallback usage
  - budget pressure / page-update counts
- focused automated coverage exists for:
  - feedback-driven request generation
  - request deduplication
  - content-hash invalidation
  - deterministic eviction ordering
  - invalid-page/coarse-fallback behavior

Until that milestone is reached:

- VSM remains `in_progress`
- conventional directional CSM remains the default-safe directional path
- `prefer-virtual` may still fall back
- `virtual-only` remains a validation mode, not a final shipping default

#### Directional Cache Realignment Plan (March 10, 2026)

Review correction: the current directional VSM slice is still functional, but
it is not yet meeting the intended resident-page reuse/invalidation bar. The
next implementation work must proceed in the following order so VSM keeps
working while cache semantics are realigned.

Update, March 10, 2026: corrected back to `in_progress`. The CPU-side
cache/invalidation work landed in code and passed the `build-vs` LightManager
suite, but runtime directional VSM still has two unresolved design gaps:
request generation and lighting do not agree on the directional clip chain,
and feedback-driven fine-page demand is still inflated into coarse per-clip
rectangles. Those gaps can manifest as angle-dependent quality collapse,
over-selection in dense scenes, and unstable dynamic-shadow updates. The
remaining conservative fallback is still that invalidation escalates to
whole-product when caster count/order changes and bounds cannot be paired
spatially.

Update, March 10, 2026 follow-up: the two runtime gaps above are now addressed
in code and covered by `build-vs` automation, but the overall directional VSM
slice remains `in_progress` until `RenderScene`/Sponza visual validation
confirms stable shadows under camera rotation and dynamic-object motion.

Update, March 10, 2026 debug pass correction: the temporary directional
page-line instrumentation is no longer the active VSM path. The shader-side
GPU debug rectangles and forced no-op visibility have been removed again, and
directional VSM sampling is back on the normal visibility path while atlas
inspection moves to the floating `RenderScene` ImGui window described below.
Status remains `in_progress` until the atlas inspector is live and validated in
scene.

Update, March 10, 2026 debug pass follow-up: the initial rectangle
reconstruction used the forward light-view XY rows instead of the inverse-basis
XY columns, which sheared the page footprints across the world. The debug path
now reconstructs world-space corners from the rigid inverse XY basis, but
overall status remains `in_progress` until the scene overlay confirms sane
page footprints and the underlying selection issue is isolated.

Update, March 10, 2026 request-footprint follow-up: the GPU request pass no
longer estimates directional receiver footprint from only right/down depth
neighbors. It now evaluates left/right and up/down candidates and keeps the
shorter valid delta on each axis, which is intended to stop camera-rotation
from inflating page demand at object silhouettes. This is validated by shader
rebuild plus `build-vs` LightManager automation, but runtime status remains
`in_progress` until the overlay confirms the red/orange object-edge halos
actually recede in scene.

Update, March 10, 2026 debug overlay follow-up: the request-pass rectangle
experiments are now superseded by the atlas-inspector path. They are kept only
as historical investigation notes and are not the active runtime debug model.

Update, March 10, 2026 debug overlay plane correction: the temporary attempt to
draw request rectangles on the camera-aligned light-space Z plane was wrong for
runtime inspection because it placed the overlay at the camera depth. The VSM
request-pass rectangle projection experiments were not the right debug model for
“pages as actually used by shading.” The overlay is being moved back to the
directional shading path so it is emitted from the real sampled clip/page at
the real receiver light-space depth, with a page-center gate to keep the view
readable. Status remains `in_progress` pending runtime confirmation.

Update, March 10, 2026 debug overlay stability follow-up: the request-pass
overlay no longer colors a page by whichever pixel first wins the atomic page
request, and it no longer reconstructs page corners from that pixel's
light-space depth. Debug page colors are now derived only from stable page
properties (clip level and residency), and rectangles are drawn on a stable
light-space debug plane. This removes per-frame GPU scheduling noise from the
overlay so remaining changes should reflect real paging churn rather than debug
artifacts.

Update, March 10, 2026 atlas inspector correction: the line-based page overlay
experiments were not sufficient for diagnosing the remaining directional paging
instability in scene, and treating them as the mainstream debug path was a
scope mistake. The active debug direction is now a floating `RenderScene` ImGui
window that displays a compute-generated `RGBA8` visualization of the physical
VSM atlas itself. The work must:
- dispatch a dedicated atlas-debug compute shader after virtual shadow raster
  has produced the current physical pool contents
- expose the resulting debug texture through the existing ImGui D3D12 backend
  without adding side-panel shell UI or unrelated CVars
- keep status `in_progress` until the floating atlas window is visible in
  `RenderScene` and shows stable per-frame atlas state for a static scene

Update, March 10, 2026 line-debug removal follow-up: the shader-side page-line
debug path is now fully removed from the active directional VSM shaders, the
floating `RenderScene` atlas-inspector path builds again, and current
automation remains green in `build-vs`. Validation evidence:
- `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:6 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- source verification: no remaining `GpuDebug`/page-line helper references in
  `ShadowHelpers.hlsli` or `VirtualShadowRequest.hlsl`
- status remains `in_progress` until the atlas window is visually confirmed in
  runtime

Update, March 10, 2026 cache-churn correction: the atlas-inspector runtime
showed that static-scene camera motion was still blowing the directional VSM
cache. Two order-sensitive invalidation sources were identified and corrected:
- the VSM backend now canonicalizes the shadow-caster bounds set before
  hashing, storing, and pairwise dirty-page comparison, so visibility-driven
  item reorder no longer looks like a different caster set
- `Renderer::HashPreparedShadowCasterContent` now hashes the shadow-caster draw
  set in canonical order instead of view-sorted draw order, so camera motion no
  longer poisons the coarse content hash for static scenes
- validation remains required in `RenderScene`; overall status stays
  `in_progress` until the atlas inspector shows bounded churn for static-scene
  camera movement

Update, March 10, 2026 resize-path correction: window resize exposed a stale
depth-SRV recovery bug in `LightCullingPass`, where a failed `UpdateView()`
path immediately tried to `RegisterView()` again and hit the registry's
duplicate-view abort. The pass now follows the same resize-safe recovery pattern
as other texture-SRV users by first recovering an existing SRV index from
`ResourceRegistry` before allocating or re-registering. Runtime status remains
`in_progress` until `RenderScene` resize is revalidated manually.

Update, March 10, 2026 feedback/cache scope correction: atlas-inspector runtime
validation showed that the earlier request-feedback realignment was incomplete.
The current implementation still stores feedback as local page indices keyed by
an origin-sensitive directional lattice hash, so ordinary camera motion can
invalidate reusable fine-page demand even when the resident cache key remains
stable. When that happens, the CPU bootstrap path still merges all visible
receiver bounds into one dense rectangle per fine clip, which can saturate the
atlas and evict useful clean pages during motion. The request-feedback/cache
scope is therefore still `in_progress` until the following are corrected in
code and validated in `build-vs`:
- request feedback must carry absolute resident-page identity instead of local
  page indices tied to clip origin
- feedback compatibility must be keyed to directional address space
  (light-basis plus per-clip page world size), not clip origin
- current-frame fine-page bootstrap must stay sparse per receiver instead of
  merging the whole receiver set into one clip rectangle
- cache preservation under repeated camera strafing must be covered by
  functional/integration tests before status can move forward

Update, March 10, 2026 resident-key feedback and sparse bootstrap correction:
the request-feedback/cache correction is now implemented in code and validated
in `build-vs`.
- `VirtualShadowRequestPass` now translates GPU request bits into absolute
  resident-page keys using the clip-grid origins from the metadata snapshot
  that produced the request mask
- feedback compatibility now matches directional address space only:
  light-basis orientation and per-clip page world size must match, while
  page-aligned clip-origin motion no longer invalidates reusable fine-page
  demand
- the fine-clip bootstrap path now marks per-receiver regions directly instead
  of merging the whole receiver set into one clip rectangle
- new LightManager regressions cover both resident-key reuse across clip-origin
  motion and sparse current-frame bootstrap behavior
- validation evidence:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - result: `25/25` VSM-focused LightManager tests passed
  - result: `37/37` LightManager/default tests passed
  - result: `RenderScene` rebuilt successfully against the updated backend
- overall status remains `in_progress` until runtime atlas/shadow behavior is
  visually confirmed in `RenderScene`

Update, March 10, 2026 renderer timing correction:
runtime troubleshooting in `virtual-only` mode exposed a renderer-side timing
bug outside the cache/invalidation slice. `LightCullingPass` was calling
`Renderer::UpdateCurrentViewLightCullingConfig()`, which republished the entire
current-view binding block and re-entered `ShadowManager::PublishForView()` after
`VirtualShadowPageRasterPass` had already run. That could replace the live
virtual shadow binding set with a fresh publication immediately before
`ShaderPass`, even though no new virtual pages had been rasterized for that
second publication. The corrected scope is:
- keep the current shadow binding publication stable for the rest of the frame
  once the VSM raster phase has run
- allow mid-frame light-culling updates to republish only the lighting routing
  payload while reusing the already-published draw/environment/view-color/debug
  and shadow slots for the active view
- keep overall feature status `in_progress` until `RenderScene` confirms that
  virtual-only shading no longer loses shadows in the simple two-cube scene
  and the atlas no longer goes effectively blank after the clustered-lighting
  republish path runs

1. [x] Canonicalize directional resident-page identity and completion state
   - introduce one shared helper for packing/unpacking the resident key
     `(clip_level, snapped_grid_x, snapped_grid_y)`
   - use that helper in request selection, resident-page lookup,
     `MarkRendered`, eviction, and introspection
   - keep the current shader-visible page-table format unchanged so shading
     remains stable while CPU-side cache bookkeeping is corrected
   - exit gate: identical-input publish promotes pages to `ResidentClean` and
     clean-page reuse tests pass again

2. [x] Preserve reuse across page-aligned directional clipmap motion
   - separate page-address stability from page-content validity
   - allow clean resident pages to be remapped when clip origins move by whole
     page increments but the light basis and page world coverage are unchanged
   - keep the fallback conservative: rerasterize on light-view, depth-range,
     page-size, or physical layout changes
   - exit gate: camera movement across clip snap thresholds retains overlapping
     pages instead of rerasterizing the full working set

3. [x] Realign request feedback with directional resident-page identity
   - replace local-page-index feedback with absolute resident-page keys
     `(clip_level, grid_x, grid_y)` so fine-page demand survives clip-origin
     motion
   - key feedback compatibility to directional address space only:
     light-basis orientation and per-clip page world size must match, while
     page-aligned origin shifts must not invalidate reusable feedback
   - consume translated resident-key feedback as sparse current demand, not as
     a local-page set tied to the previous clip origin
   - exit gate: feedback remains reusable across page-aligned camera motion and
     only drops when the directional address space truly changes

4. [x] Replace whole-product invalidation with spatial dirty-page invalidation
   - keep the coarse global content hash only as a conservative fallback
   - transform changed caster bounds into light space and mark only overlapping
     resident pages dirty
   - do not invalidate shadow-depth cache contents for sampling-only changes
     such as constant/normal bias
   - exit gate: moved casters rerasterize only overlapping requested pages and
     unrelated clean pages stay resident

5. [x] Fix deterministic eviction against the canonical resident key
   - evict only unrequested pages
   - order eviction by clip priority, then `last_touched_frame`, then stable
     resident key
   - remove the current dense-page-index assumptions from eviction bookkeeping
   - exit gate: eviction tests are deterministic and resident-key truncation
     warnings are gone

6. [x] Revalidate the CPU-side cache/invalidation slice in `build-vs` only
   - update renderer tests for identical reuse, clean-page retention across
     receiver shifts, clipmap-snap reuse, feedback invalidation on lattice
     change, and spatial invalidation
   - rebuild and run only against `out/build-vs`
   - validation evidence:
     - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:6 /p:Configuration=Debug /nologo`
     - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
     - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
   - keep `virtual-only` opt-in until `RenderScene` and Sponza visual
     validation confirm stable camera translation/rotation

7. [x] Keep current-frame fine-page bootstrap sparse and view-correct
   - remove the current “one merged receiver rectangle per clip” bootstrap path
     for fine directional clips
   - mark per-receiver fine-page regions directly, with small bounded guard
     growth, so current-frame demand stays sparse even before delayed feedback
     converges
   - preserve deduplication through the selected-page bitset so overlapping
     receivers do not explode the page count
   - exit gate: repeated camera strafing over the same static scene preserves a
     mostly stable atlas instead of re-requesting a dense clip rectangle

8. [ ] Realign directional request generation with shading clip selection
   - use the same footprint-driven directional clip selection policy in the
     lighting path that the request pass uses to generate demand
   - stop starting virtual directional shading from the finest containing clip
     when that clip is not the intended shading footprint
   - preserve bounded fallback by walking to coarser valid clips only when the
     footprint-selected clip is unmapped
   - implemented in code on March 10, 2026 and covered by shader compilation
     plus `build-vs` LightManager regression tests
   - remaining delta: visual validation in `RenderScene`/Sponza under camera
     rotation and moving casters
   - exit gate: camera rotation at fixed translation no longer causes
     angle-dependent stripe artifacts or runaway fine-page demand

8. [ ] Replace rectangular feedback inflation with sparse fine-page refinement
   - stop collapsing all feedback pages in a clip into one bounding region
   - retain sparse page demand, then apply only a bounded local guard/dilation
     around requested pages
   - keep the current coarse backbone and mapped-page grace behavior so
     fallback coverage remains stable
   - implemented in code on March 10, 2026 and covered by a new sparse-page
     integration regression in `LightManager_basic_test.cpp`
   - remaining delta: visual validation in complex scenes with scattered
     visible demand
   - exit gate: dense/complex scenes do not explode fine-page selection simply
     because visible demand is spatially scattered

### 8.6 Local Light Shadow Path

- [ ] Implement spot-light shadow allocation, rendering, and sampling
- [ ] Implement point-light shadow allocation, rendering, and sampling
- [ ] Add mixed-mobility cache and invalidation flow for local light shadows
- [ ] Expand the virtual backend to support spot and point products on the same
      local-shadow contract

### 8.7 Contact Shadow Refinement

- [ ] Implement contact shadow mask generation and composition per authored
      light settings
- [ ] Integrate contact shadow evaluation into receiver lighting

### 8.8 Validation and Hardening

- [ ] Add automated coverage for shadow extraction, scheduling, GPU data
      packing, and sampling invariants
- [ ] Add automated coverage for virtual residency, page-table, and fallback
      invariants
- [ ] Add visual debug modes and validation scenes for cascade, virtual
      coverage, atlas/array, bias, and receiver diagnostics
- [ ] Profile and tune quality tiers, budgets, family-selection policy, and
      invalidation behavior

---

## Phase 9 – Skinned Mesh Rendering

Enable skeletal animation for characters and creatures.

### 9.1 Vertex Format Extension

- [ ] Add `bone_indices` (uint4) and `bone_weights` (float4) to `Vertex`
- [ ] Update HLSL vertex input layout in `ForwardMesh.hlsl`
- [ ] Detect skinned vs static by mesh type for backwards compatibility

### 9.2 Skeleton Runtime

- [ ] Define `Skeleton` class: bone hierarchy, bind poses, inverse bind matrices
- [ ] Define `SkeletonInstance` class: per-instance bone transforms
- [ ] Create bone transform buffer SRV (structured buffer of float4x3)

### 9.3 PAK & Loader

- [ ] Define `SkinnedMeshInfo` in `MeshDesc.info`
- [ ] Update `GeometryLoader` to handle `kSkinned` mesh type

### 9.4 Shader Integration

- [ ] Add `HAS_SKINNING` shader permutation
- [ ] Implement vertex skinning: blend position/normal by bone weights

---

## Phase 10 – Advanced PBR: Clear Coat

Add clear coat layer for automotive paint, wet surfaces, lacquered wood.

### 10.1 MaterialShadingConstants Extension

- [ ] Add `clearcoat_factor`, `clearcoat_roughness`, texture indices
- [ ] Update HLSL `MaterialShadingConstants.hlsli` to match

### 10.2 MaterialAsset & MaterialBinder

- [x] Add clearcoat getters to `MaterialAsset`
- [ ] Update `SerializeMaterialShadingConstants()` to populate clearcoat fields

### 10.3 Shader Implementation

- [ ] Add second specular lobe in `ForwardPbr.hlsli`
- [ ] Attenuate base layer by clearcoat Fresnel

---

## Phase 11 – Resource Management

Improve GPU memory efficiency.

### 11.1 Mesh Eviction

- [ ] Implement `EvictUnusedMeshResources(frame_id, age_threshold)`

### 11.2 Buffer Pooling

- [ ] Pool light grid/index buffers
- [ ] Pool post-process intermediates

### 11.3 Metrics

- [x] Add per-subsystem allocation/eviction diagnostics in resource/upload code
- [ ] Add unified renderer debug counters: allocations, evictions, peak usage

---

## Phase 12 – Volumetric Fog

Implement real volumetric fog as a screen-space or froxel-based effect.

> **Note**: A previous per-mesh fog implementation was removed because it was
> mutually exclusive with Aerial Perspective (never ran when AP was enabled),
> only affected mesh surfaces (no atmospheric haze), and was redundant since
> Aerial Perspective already provides physically-based distance attenuation.
> This phase implements a proper volumetric fog system.

### 12.1 Design Decisions

- [ ] Choose implementation approach:
  - **Froxel-based**: 3D volume texture (frustum-aligned voxels), raymarch in
    compute, temporal reprojection. Higher quality, works with local lights.
  - **Screen-space**: Per-pixel raymarch in post-process, simpler but no
    light scattering from positional lights.
  - **Hybrid**: Screen-space with clustered light contribution lookup.
- [ ] Define relationship with Aerial Perspective:
  - Option A: Replace AP when fog enabled (fog takes over distance effects)
  - Option B: Combine additively (fog on top of AP inscattering)
  - Option C: User choice via `FogMode` enum

### 12.2 Fog Volume Infrastructure

- [ ] Create `VolumetricFogPass` class in `src/Oxygen/Renderer/Passes/`
- [ ] Create froxel volume texture (e.g., 160×90×128, RGBA16F)
- [ ] Define `GpuVolumetricFogParams` struct:
  - `extinction_sigma_t_per_m`, `height_falloff_per_m`, `height_offset_m`
  - `single_scattering_albedo_rgb`, `anisotropy_g`
  - `temporal_blend_factor`, `max_distance`
  - `extinction_coefficient`, `ambient_contribution`
- [ ] Add to `EnvironmentStaticData` alongside existing `GpuFogParams`

### 12.3 Fog Density Computation (Compute Pass)

- [ ] Create `VolumetricFogDensity_CS.hlsl`:
  - Thread group: 8×8×1, dispatch covers froxel grid
  - Sample noise texture for density variation
  - Apply exponential height falloff
  - Write scattering + extinction to froxel volume
- [ ] Support local fog volumes (box/sphere regions with custom density)
- [ ] Inject positional light contributions into froxels (clustered lookup)

### 12.4 Fog Raymarch & Accumulation

- [ ] Create `VolumetricFogRaymarch_CS.hlsl`:
  - Front-to-back raymarch through froxel volume
  - Accumulate inscattering with extinction
  - Apply Henyey-Greenstein phase function for sun scattering
  - Output: integrated inscatter RGB + transmittance A
- [ ] Temporal reprojection using motion vectors:
  - Blend with previous frame's fog result
  - Handle disocclusion with confidence weights

### 12.5 Fog Application

- [ ] Create `VolumetricFogApply_PS.hlsl` post-process:
  - Sample fog accumulation texture
  - Blend with scene color: `color = color * transmittance + inscatter`
- [ ] Insert after `TransparentPass`, before `PostProcessPass`
- [ ] Option to apply during forward pass (sample froxel in `ForwardMesh_PS`)

### 12.6 Debug Panel Integration

- [ ] Add Fog section back to `EnvironmentDebugPanel`:
  - Enable/disable toggle
  - Density, height falloff, height offset sliders
  - Albedo color picker
  - Anisotropy, scattering intensity sliders
  - Max distance, temporal blend controls
- [ ] Add fog volume visualization mode (render froxel slices)
- [ ] Add fog-only debug view (show inscatter without scene)

---

## Ongoing – Performance & Quality

Continuous improvements, not milestones:

- Hierarchical culling (BVH/octree) when profiling shows extraction bottleneck
- Per-pass GPU timing queries
- Screen-space error LOD selection (integrate with existing policies)
- Unit tests for sort keys, cluster math, eviction
- Doxygen for public APIs

---

## Deferred (Reassess Later)

**Rendering Features**:

- Order-independent transparency (per-pixel linked lists, weighted blended)
- Screen-space reflections
- Ambient occlusion (SSAO/GTAO)

**Advanced Geometry (require PAK/Vertex extension)**:

- Morph targets — PAK has `kMorphTarget` enum, but no `MorphTargetInfo` struct
- Billboard meshes — PAK has `kBillboard` enum, no runtime support
- Voxel meshes — PAK has `kVoxel` enum, no runtime support
- Collision/navigation meshes — PAK has `kCollision`/`kNavigation`, physics needed

**Advanced Materials (require PAK format extension)**:

- Height/parallax mapping — no PAK fields
- Subsurface scattering (skin, wax, foliage) — no PAK fields
- Anisotropy (hair, brushed metal) — no PAK fields
- Detail textures / secondary UV — no PAK fields

**Advanced Materials (already in PAK, lower priority)**:

- Transmission (glass, thin foliage) — PAK has `transmission_factor`, etc.
- Sheen (cloth, fabric) — PAK has `sheen_color_texture`, `sheen_color_factor[3]`
- Specular/IOR override — PAK has `specular_texture`, `specular_factor`, `ior`
- Decals

**Override Attachments (future domains)**:

- Physics domain properties (`phys_layer`, `phys_mass_mult`)
- Audio domain properties (`aud_reverb_zone`, `aud_occlusion`)
- Gameplay domain properties (`game_faction`, `game_interact`)
- Editor domain properties (`edtr_lock`, `edtr_gizmo`) — strip in shipping

**Infrastructure**:

- Material shader graph / node editor
- Hot reload for materials/shaders
- Ray tracing integration
- Automated render graph resource solver
- Bindless texture support for Material Instances (batch texture-varying MIs)

---

## Revision History

- **March 9, 2026**: Completed the multi-technique shadow remediation for the
  working conventional raster path: split shading publication from raster
  planning, introduced `ConventionalShadowBackend`, replaced the
  directional-only raster pass with `ConventionalShadowRasterPass`, moved
  shading dispatch to `ShadowInstanceMetadata`, and added focused automated
  coverage for invalidation and shadow-only caster routing.
- **March 9, 2026**: Tightened the conventional directional shadow plan to
  require engine-grade cascaded shadow-map hardening: stable texel-snapped
  fitting, tighter depth fitting, comparison sampling, kernel-aware padding,
  and combined raster/receiver bias policy before virtual-shadow expansion.
- **March 8, 2026**: Added required shadow runtime diagnostics to the shadow
  design so publication, resource allocation/reuse, binding publication, and
  shadow-pass draw emission are observable without a GPU capture.
- **March 8, 2026**: Expanded the shadow design and execution plan to support
  multiple runtime implementation families under one shared shadow-product
  contract, including virtual shadow maps as a first-class target.
- **March 7, 2026**: Moved shadow design/specification out of
  `implementation_plan.md` into `shadows.md`. Rewrote Phase 8 as execution-only
  tracking aligned with the dedicated shadow design document.
- **January 10, 2026**: Added Phase 12 – Volumetric Fog. Removed previous
  per-mesh fog implementation (was redundant with Aerial Perspective and only
  affected mesh surfaces). New phase covers froxel-based volumetric fog with
  temporal reprojection, light scattering, and proper screen-space haze.
  Total: 12 phases.
- **January 7, 2026**: Merged completed phases (Shader Permutations, Emissive,
  GPU Instancing) into Current State. Added phases for override_slots.md design:
  Light Channel Masks (Phase 2), Override Attachments (Phase 3), Material
  Instances (Phase 4), Extended GPU Instancing 64-byte params (Phase 5).
  Renumbered remaining phases. Total: 11 phases.
- **January 7, 2026**: Consolidated plan from 16 to 11 phases. Removed Draw
  Packet Abstraction (redundant with GPU Instancing). Moved Transmission to
  deferred (niche use case). Moved Height/Parallax to deferred (requires PAK
  changes). Converted Performance/Hardening to Ongoing section.
- **January 7, 2026**: Added comprehensive geometry analysis to baseline.
  Added GPU Instancing and Skinned Mesh Rendering phases.
- **January 7, 2026**: Grounded all tasks in actual codebase analysis.
- **January 7, 2026**: Initial rewrite from legacy format.
- **December 2025**: Phases 1–4 (original numbering) completed.
