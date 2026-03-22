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
- March 19, 2026 follow-up: directional VSM receiver bias is now being moved
  onto explicit renderer CVars and named virtual metadata fields instead of
  zero-default synthetic-sun publication plus opaque shader constants. Status
  remains `in_progress` until static `physics_domains` captures prove proper
  shadow coverage without scene-wide acne.
- March 19, 2026 close-camera follow-up: the virtual receiver path no longer
  lets receiver bias reselect a different directional clip family. Clip
  selection now stays anchored to the unbiased receiver and the bias only
  affects the within-family compare sample. This removed the large concentric
  floor rings seen in the live `physics_domains` close-camera compare capture,
  but residual underside/contact mismatch remains, so directional VSM bias
  hardening stays `in_progress`.
- March 19, 2026 live-scene permanence follow-up: the directional VSM resolve
  path no longer widens its hard-shadow filter footprint per requested clip and
  no longer scales receiver bias in discrete clip-family steps. Resolve now
  keeps a single hard-shadow comparison footprint, relies on page-table-driven
  coarser fallback remap only, and derives receiver bias from the continuous
  clip level instead of the requested clip index. The temporary clip-ID
  coloring that was added to `kVsmResolve` for diagnosis was removed so the
  mode again reports actual requested-vs-resolved health. Validation on the
  untouched live scene used:
  `current-live-vsmresolve-final-20260319.bmp`,
  `current-live-vsmstored-fixed-20260319.bmp`,
  `current-live-vsmreceiver-fixed-20260319.bmp`,
  `current-live-vsmcompare-fixed-20260319.bmp`, and
  `current-live-virtual-fixed-20260319.bmp`.
  Those captures show a full-green resolve view, smooth stored/receiver depth
  gradients, and no visible right-half clip-shaped lighting wedge in the live
  virtual frame. Status remains `in_progress` for broader UE5 parity until the
  renderer has automated neighboring-clip equivalence coverage instead of only
  build-plus-capture evidence.
- March 19, 2026 default-value follow-up: the code-path defaults for synthetic
  directional bias and virtual directional bias scales were aligned with the
  tuned archived CVar values so a cold start without `bin/Oxygen/cvars.json`
  still boots into the proven bias configuration. The no-archive defaults now
  match `receiver_normal=1.0`, `receiver_constant=0.0`,
  `receiver_slope=0.5`, `raster_constant=0.1`, and `raster_slope=0.35`.
- March 19, 2026 fallback-stability follow-up: live-scene `kVsmResolve`
  captures later showed that the remaining yellow areas were real coarser-page
  fallback, not the earlier clip-family debug coloring. The unresolved bug was
  that virtual page rasterization still authored a different stored-depth
  solution for coarser pages because `DepthPrePass.hlsl` scaled raster bias and
  guard footprint directly from the discrete clip index. That path now matches
  the resolve-side permanence fix: raster hard-shadow authoring keeps a
  one-texel footprint, derives raster bias from the continuous clip level of
  the unbiased receiver, and no longer bakes a different answer just because a
  coarser fallback page was used. The untouched live-scene validation first
  used `current-live-vsmresolve-check-20260319.bmp` plus
  `current-live-virtual-hypothesis-20260319.bmp`, which correlated broad
  fallback with visibly wrong unstable striping. After the raster-bias fix the
  normal frame improved (`current-live-virtual-rasterfix2-20260319.bmp`) but
  resolve still showed too much fallback
  (`current-live-vsmresolve-rasterfix2-20260319.bmp`), so the slice remained
  `in_progress`.
- March 19, 2026 ultra-capacity follow-up: the same live-scene logs showed the
  renderer was already at the `Ultra` directional VSM budget with
  `pages_per_clip_axis=64`, `clip_levels=12`, and only `physical_tiles=6144`,
  so some camera angles were legitimately over capacity. `ResolvePhysicalTileCapacity`
  now raises `ShadowQualityTier::kUltra` to `8192` tiles, which published a
  `11648x11648` physical page pool in the new live captures. Sequential
  validation on the untouched live scene used
  `current-live-vsmresolve-capfix-20260319.bmp`,
  `current-live-virtual-capfix-20260319.bmp`, and
  `current-live-vsmcompare-capfix-20260319.bmp`. Those captures removed the
  broad yellow fallback bands from resolve, removed the corresponding unstable
  striping from the normal render, and left only localized residual mismatch
  around thin geometry/foliage in compare. Status for this fallback-stability
  slice is `completed` for the current live-scene repro, while broader UE5
  parity remains `in_progress` until automated neighboring-clip equivalence and
  residency-pressure coverage exist.
- March 19, 2026 cleanup follow-up: the permanence work no longer carries the
  dead directional clip-radius selector plumbing. The no-op
  `SelectDirectionalVirtualFilterRadiusTexels` helpers were removed from the
  resolve shader, raster shader, lighting common include, and backend, the
  atlas-UV helper no longer accepts a hardcoded `1u` radius argument, and the
  temporary unused-parameter shims in the touched shaders were removed so the
  code reflects the fixed one-footprint hard-shadow contract directly.
- March 19, 2026 UE5-parity execution plan for directional VSMs:
  the current live-scene fixes removed the visible clip-family and fallback
  instability bugs, but Oxygen still diverges from UE5's directional contract
  in several structural places. The remaining work must be implemented in this
  order and validated after each step on the untouched current live scene:
  1. Publish UE-style directional projection metadata.
     Oxygen currently encodes clip selection through
     `clipmap_world_origin_resolution_lod_bias` plus per-clip page/depth fields in
     `DirectionalVirtualShadowMetadata`, while UE publishes explicit per-clip
     projection data (`ClipmapWorldOrigin`, `ResolutionLodBias`,
     `ClipmapCornerRelativeOffset`, `ClipmapLevel`,
     `ClipmapLevelCountRemaining`, and projection matrices) in
     `VirtualShadowMapProjectionStructs.ush`. We need matching published data
     in `DirectionalVirtualShadowMetadata.{h,hlsli}` and
     `VirtualShadowMapBackend.cpp` so shader selection/remap no longer depends
     on Oxygen-only encodings.
  2. Replace nearest-clip search with exact requested-clip selection.
     `SelectDirectionalVirtualClipForDistance` in `ShadowHelpers.hlsli`
     currently computes a continuous level and then searches neighboring clips
     until one projects. UE's `CalcAbsoluteClipmapLevel` / `CalcClipmapLevel`
     choose the requested clip directly from the unbiased receiver. Oxygen must
     switch to that exact contract and stop using neighbor search as part of
     ordinary selection.
  3. Refactor fallback lookup to UE-style requested-basis remap.
     `TryResolveDirectionalVirtualPageLookup` and
     `BuildDirectionalVirtualClipRelativeTransform` already remap resolved
     pages/depth back to requested basis, but they still reconstruct the
     coarser page through float projection. UE's `SampleVirtualShadowMapClipmap`
     uses page-table-driven coarser fallback plus integer page remap
     (`CalcClipmapOffsetLevelPage`) and relative clip transforms
     (`CalcClipmapRelativeTransform`). Oxygen must move to the same requested-
     basis integer remap contract.
  4. Split direct compare from filtered paths.
     Oxygen's main directional path still runs through
     `SampleVirtualShadowComparisonTent3x3PageAware`, while UE's direct path is
     a single compare sample and keeps softening in a separate SMRT path.
     Oxygen must make direct compare the authoritative baseline and treat any
     filtered/soft shadow path as an explicit separate mode.
  5. Align CPU/test helpers with shader behavior.
     The shader slope-bias scale already matches UE's exponential clip offset,
     but `ComputeDirectionalVirtualFallbackSlopeBiasScale` in
     `ShadowBackendCommon.h` still returns `fallback_lod_offset + 1`. That
     helper and its focused tests must be corrected or removed so CPU/test
     expectations match the live shader contract.
  6. Add parity debug views and capture gates.
     Existing `kVsmResolve`, stored-depth, receiver-depth, and compare captures
     stay in the loop. Add additional directional VSM debug outputs for:
     requested clip id, sampled clip id, fallback clip delta, requested depth,
     remapped stored depth, and post-remap depth delta. Each parity step keeps
     status `in_progress` until the new debug views show requested-vs-sampled
     agreement consistent with UE's contract on the current live scene.
  7. Validate every step through build plus live-scene captures.
     Required evidence after each slice:
     `cmake --build out/build-ninja --config Release --target oxygen-examples-renderscene`
     plus the current live-scene capture scripts for no-debug, `kVsmResolve`,
     compare, stored-depth, receiver-depth, and any new parity debug views
     added in step 6.
- March 19, 2026 UE5-parity step 1/2 follow-up: Oxygen now publishes explicit
  directional clip-level projection metadata instead of relying only on the
  old `clipmap_world_origin_selection` encoding. The published directional VSM
  metadata now carries UE-style naming for the clipmap world origin plus
  resolution-lod-bias field and per-clip level/count data in
  `DirectionalVirtualShadowMetadata.{h,hlsli}` and
  `VirtualShadowMapBackend.cpp`. The selection path in `ShadowHelpers.hlsli`
  no longer searches finer/coarser neighboring clips after picking a target
  level; it resolves one requested clip index directly from the unbiased
  receiver and either projects in that clip or fails. Initial validation for
  this slice used build plus the untouched live-scene captures
  `current-live-virtual-ueclipsel-20260319.bmp` and
  `current-live-vsmresolve-ueclipsel-20260319.bmp`, and that validation
  regressed badly: the resolve view became majority magenta and the normal live
  frame showed visibly wrong shadows. That proves exact requested-clip
  selection is not production-correct yet without the next fallback-remap
  parity slice. Status for metadata publication is `completed`, but exact
  requested-clip selection remains `in_progress` until the UE-style requested-
  basis fallback remap is implemented and revalidated.
- March 19, 2026 UE5-parity step 3/4/5/6 follow-up: the requested-basis
  fallback remap is now in code in `ShadowHelpers.hlsli`, using integer
  requested-page remap plus per-clip origin metadata instead of the earlier
  float-only fallback lookup. The baseline directional virtual hard-shadow path
  now resolves through a single compare sample instead of the previous 3x3
  page-aware tent path, and the stale CPU/test fallback slope-bias helper in
  `ShadowBackendCommon.h` now matches the shader's power-of-two sampled-vs-
  requested clip scaling. Focused validation for the helper was executed with
  `Oxygen.Renderer.VirtualShadowContracts.Tests.exe --gtest_filter=*VirtualShadowFallbackContracts*`
  and passed `7/7` tests. The renderer also now carries explicit live-scene
  debug views for requested clip, resolved clip, clip delta, and post-remap
  depth delta. Live-scene captures `current-live-virtual-remap-20260319.bmp`,
  `current-live-vsmresolve-remap-20260319.bmp`,
  `current-live-vsmrequestedclip-raw-20260319.bmp`,
  `current-live-vsmresolvedclip-20260319.bmp`,
  `current-live-vsmclipdelta-20260319.bmp`, and
  `current-live-vsmdepthdelta-20260319.bmp` showed that the large clip/fallback
  failure was gone, resolve was overwhelmingly green again, and the remaining
  mismatch had collapsed to foliage and other thin geometry.
- March 19, 2026 UE5 exact-requested retry follow-up: after the requested-basis
  remap landed, I retried the literal distance-derived requested clip contract
  in both `ShadowHelpers.hlsli` and `Lighting/VirtualShadowRequest.hlsl`. That
  retry was invalid. The untouched live-scene captures
  `current-live-virtual-ueexact-20260319.bmp` and
  `current-live-vsmresolve-ueexact-20260319.bmp` showed the same bad regression
  again, with resolve becoming majority magenta. I reverted that slice and
  revalidated the live scene with `current-live-virtual-postrevert-20260319.bmp`
  and `current-live-vsmresolve-postrevert-20260319.bmp`, which restored the
  previous stable state. Current production code therefore still uses the
  smallest-containing projected clip for live sampling and requests, while the
  exact UE `CalcClipmapLevel`-style requested-clip contract remains
  `in_progress`.
- March 19, 2026 UE5 exact-requested completion follow-up: the failed retry
  above was caused by Oxygen still publishing a mixed clip-selection/bias
  metadata contract. The backend now publishes separate selection inputs and
  receiver-bias inputs in `DirectionalVirtualShadowMetadata.{h,hlsli}`:
  a camera-origin, viewport/projection-derived selection lod bias, plus
  absolute per-clip levels for the requested-clip estimator, while preserving
  the existing receiver-bias continuous clip field for raster/resolve bias
  scaling. With that split in place, the exact requested-clip path was retried
  in `ShadowHelpers.hlsli` and `Lighting/VirtualShadowRequest.hlsl` and
  revalidated on the unchanged live scene. The live captures
  `current-live-vsmresolve-ueexact2-20260319.bmp`,
  `current-live-virtual-ueexact2-20260319.bmp`, and
  `current-live-vsmcompare-ueexact2-20260319.bmp` stayed on the stable path:
  resolve remained overwhelmingly green and the normal frame did not regress.
  Focused contract coverage also still passed via
  `Oxygen.Renderer.VirtualShadowContracts.Tests.exe --gtest_filter=*VirtualShadowFallbackContracts*:*VirtualShadowClipmapContracts*`
  with `16/16` tests passing. Status for exact requested-clip selection is now
  `completed`; the remaining directional VSM parity gap is concentrated on
  thin/masked geometry mismatch rather than clip-family selection.
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
  - a persistent per-view shader-visible page table with a stable bindless
    slot; each publish stages the current entries for pass-time GPU upload
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
  Requests are now bounded and cross-frame reusable, and the contract
  convergence work for the current directional slice is landed: the runtime now
  publishes one authoritative resolved-page raster contract and virtual raster
  consumes only that contract. No parallel legacy raster-job path remains. The
  remaining gate for this slice is live validation in `RenderScene` and Sponza;
  current-frame resolve/update ownership is still CPU-authored inside the
  explicit resolve stage. Budgeted VSM page/update
  policy is deferred until after correctness and quality are closed.
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
- [~] Add sparse receiver-driven requests, GPU residency resolve, and final
      resolve-to-raster scheduling
  - request generation and live resolve compaction now both exist in the frame
    graph
  - design correction: the current live resolve bridge is observational only
    and must not suppress CPU-authored fine pending raster jobs yet
  - step 1 landed: `VirtualShadowRenderPlan` now exposes one authoritative
    resolved-page raster contract and `VirtualShadowPageRasterPass` consumes
    only that contract
  - step 2 landed: the current CPU planner now bridges into that contract
    without changing live visual behavior
  - step 3 landed: publish-time resolved-page duplication is gone; the resolve
    handoff now materializes the current-frame resolved-page contract from
    backend-private pending jobs just before raster consumption
  - step 4 bridge slice landed: `VirtualShadowResolvePass` now explicitly owns
    the current-frame CPU residency / allocation / page-table preparation
    ordering before upload and raster consumption, and
    `PreparePageTableResources` no longer recomputes that state on demand
  - step 4 hardening slice landed: the temporary observation-only VSM
    introspection exports were fenced off from runtime ownership
  - step 4 ownership slice landed: `VirtualShadowResolvePass` now prepares the
    current-frame page-table upload for every virtual frame, and the virtual
    raster / atlas-debug passes now consume published exports without calling
    resolve or page-table prep themselves
  - step 4 cleanup slice landed: `MarkRendered` is now post-raster bookkeeping
    only and no longer backdoor-resolves current-frame allocation/page-table
    state; the atlas-debug cached-vs-reused regression now explicitly resolves
    the first frame before marking it rendered
  - step 4 completion slice landed: publish-time view-state construction now
    only snapshots the authoritative prior resident state, and the explicit
    resolve stage owns current-frame carry, dirty marking, tile release,
    allocation, eviction, and page-table mutation as one CPU-authored stage
  - current-frame resolve/update ownership is still CPU-authored inside the
    explicit resolve stage, but the legacy provisional raster-job contract is
    removed and `VirtualShadowResolvedRasterPage` is now the only live raster
    payload exported to runtime consumers
  - frozen execution order for the remaining directional VSM work (steps 1-5
    landed; 6 remains):
    6. validate `virtual-only` stability and quality in `RenderScene` and
       Sponza
  - do not reorder these steps unless new evidence forces a documented scope
    correction under the repository guardrails
- [x] Harden directional virtual invalidation and focused regression coverage
- [x] Remove the provisional legacy raster-job scheduling path so the
      resolve-owned raster-page contract is the only live raster contract

Directional VSM Milestone: `Directional VSM Production Candidate`

This is the next milestone worthy of being treated as a real delivery gate.
It is reached only when all of the following are true:

- request generation is feedback/depth-driven with no steady-state dependency
  on CPU visible-receiver fallback
- request deduplication, residency updates, and raster-page scheduling are
  active end to end from one authoritative resolve path
- resident-page reuse, invalidation, and eviction are deterministic and
  content-safe under moving/rotating casters
- `virtual-only` is visually stable in `RenderScene` and Sponza under camera
  translation/rotation
- edge quality is brought to parity-targeting territory with the current
  conventional directional path, rather than remaining a visibly inferior
  baseline
- performance is production-acceptable in medium/large scenes; directional
  VSM must no longer collapse the renderer into single-digit FPS through page
  overproduction, brute-force raster replay, or per-frame full-buffer
  readback cost
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

#### Directional VSM Performance Recovery Plan (March 12, 2026)

The detailed performance plan now lives in
`src/Oxygen/Renderer/Docs/directional_vsm_performance_plan.md`.

Summary:

- the next blocker is performance, not correctness
- the dominant current costs are brute-force page raster replay, backend page
  overproduction, and full-buffer request/resolve readback overhead
- Step 1 baseline capture is now completed for the active moving-camera
  `RenderScene` benchmark scene; see
  `src/Oxygen/Renderer/Docs/directional_vsm_performance_plan.md`
- Step 2 page-local raster culling is now completed with measured reductions
  in steady-state rastered pages (`740.95 -> 420.75`) and shadow draw
  submissions (`6668.55 -> 1465.80`) on the superseded static-camera
  benchmark
- Step 3 page-production tightening / budgeting is now `in_progress`; the
  first guard-band tightening slice has historical static-benchmark evidence,
  and the latest moving-camera runtime win comes from capping cold/mismatch
  bootstrap to the nearest fine clips (`120144 ms -> 64755 ms` wall time);
  the newest coarse-first stress-path slice is a correctness / UX fix with
  near-neutral benchmark cost (`64755 ms -> 62911 ms`) that prevents the
  resolver from spending the last atlas tiles on fine pages while current
  coarse fallback is still missing; the fast-motion fallback hole is now
  partially mitigated by the later capacity-fit coarse safety clip and
  persistent last-coherent publish fallback work, plus the new
  fallback-recovery slice that suppresses dense unpublished fine bootstrap
  while `publish_fallback` is active. On the locked moving-camera benchmark
  that cut wall time `66602 ms -> 15773 ms`, scheduled pages
  `510.35 -> 233.29`, resolved pages `1427.18 -> 295.12`, and shadow draws
  `2086.53 -> 504.82`, while the hot fallback frames dropped from
  `selected=12300, receiver_bootstrap=12288` to
  `selected=12, receiver_bootstrap=0`. The newest publish-compatible
  stale-fallback gate now uses the actual previously published coarse coverage
  with bounded continuous receiver overrun instead of the rejected full-page
  overshoot relaxation; focused VSM tests are green at `48/48`, and the locked
  moving-camera benchmark stayed effectively flat (`15773 ms -> 16156 ms`).
  However, user live validation is still the remaining exit delta for the
  zoom/aggressive-motion wrong-page flashing fix. Step 3 remains `in_progress`
  for both the remaining publishable-frame budgeting work and that final visual
  revalidation
- the frozen execution order is:
  1. baseline capture (`completed`)
  2. page-local raster culling (`completed`)
  3. page-production tightening/budgeting (`in_progress`)
  4. readback-path reduction
  5. dynamic-pressure cache specialization
  6. before/after validation gate

Correction, March 12, 2026:

- the current directional VSM work is now frozen for redesign analysis
- authoritative review:
  `src/Oxygen/Renderer/Docs/directional_vsm_architecture_review.md`
- replacement redesign plan:
  `src/Oxygen/Renderer/Docs/directional_vsm_redesign_plan.md`
- the review concludes that the remaining motion-time wrong-page / no-shadow
  issue is architectural, not just an unresolved Step 3 heuristic
- the performance-recovery plan therefore remains historical evidence, not the
  active completion path, until a redesign plan replaces it

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

9. [ ] Replace rectangular feedback inflation with sparse fine-page refinement
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

- **March 12, 2026**: Landed the first two frozen resolve-to-raster execution
  steps for directional VSM. `VirtualShadowRenderPlan` now publishes one
  authoritative resolved-page raster contract, `VirtualShadowPageRasterPass`
  consumes only that contract, and the current CPU planner bridges pending
  raster work into it without changing live scheduling behavior. Added focused
  coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`,
  and
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  all completed successfully. Status remains `in_progress`: current-frame page
  selection, allocation, eviction, and page-table updates are still CPU-owned,
  and the readback resolve bridge was still telemetry only at that stage.
- **March 12, 2026**: Landed frozen step 3 of the directional VSM
  resolve-to-raster plan. `VirtualShadowMapBackend` no longer publishes the
  resolved-page raster contract during `PublishForView`; instead the resolve
  handoff materializes `VirtualShadowRenderPlan::resolved_pages` from
  backend-private pending jobs just before raster consumption, keeping one live
  contract while allocation/page-table ownership remains unchanged. Added
  focused coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`,
  and
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  all completed successfully. Status remains `in_progress`: allocation,
  eviction, and page-table updates are still CPU-owned and the readback
  `VirtualShadowResolvedRasterSchedule` bridge remains telemetry only.
- **March 12, 2026**: Corrected the directional VSM resolve-bridge scope after
  live camera-motion regressions. The first live `VirtualShadowResolvePass`
  bridge currently compacts only requested pages that already have valid
  current-frame page-table entries, so it is observational telemetry and not
  an authoritative raster schedule yet. Using that bridge payload to prune
  CPU-authored fine pending jobs caused coarse-only quality regressions and
  intermittent caster dropouts in `RenderScene`. The plan is corrected:
  current bridge schedules remain readback/introspection only until resolve
  owns current-frame allocation and raster scheduling end-to-end. Status
  remains `in_progress`; manual `RenderScene` revalidation is required after
  the pruning rollback lands.
- **March 12, 2026**: Fixed a runtime regression in the live directional VSM
  resolve bridge. Compatible resolved schedules were pruning fine pending
  raster jobs without also unpublishing those fine pages from the current frame
  page table, which could expose stale tile contents as one-frame garbage
  shadows during camera motion. The backend now clears pruned fine page-table
  entries so shading falls back to the coarse backbone instead. The focused
  non-regression coverage now tracks the corrected contract through
  `LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleDoesNotSuppressCpuPendingJobsYet`.
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleDoesNotSuppressCpuPendingJobsYet`,
  and
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  all completed successfully. Status remains `in_progress`: the bridge fix is
  in place, but manual `RenderScene` revalidation of the reported camera-motion
  artifact is still outstanding and the final GPU resolve-owned raster contract
  is not complete.
- **March 12, 2026**: Fixed a second runtime regression in the step-4
  resolve-owned CPU bridge. The backend was rebuilding
  `state.page_table_entries` during the lazy resolve stage but was no longer
  restaging those entries into the persistent per-view upload buffer before
  `PreparePageTableResources` copied that buffer to the GPU page table. That
  mismatch could leave the atlas visibly updated while scene shading still saw
  an all-zero page table and produced no shadows. The resolve stage now
  restages rebuilt page-table contents before the copy, and focused
  non-regression coverage now checks the copied upload payload directly through
  `LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`.
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  and
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`
  all completed successfully. Status remains `in_progress`: the live
  directional VSM path still requires manual `RenderScene` revalidation after
  this runtime fix, and the final GPU resolve-owned raster contract is not yet
  complete.
- **March 12, 2026**: Landed the next safe step-4 bridge slice for
  directional VSM. `VirtualShadowResolvePass` now explicitly executes the
  current-frame CPU residency / allocation / page-table preparation before
  later upload and raster consumption, instead of leaving
  `PreparePageTableResources` to lazily recompute that state on demand.
  `VirtualShadowMapBackend::ResolveCurrentFrame` is now the single helper used
  by the live resolve pass and the remaining compatibility accessors, and the
  focused page-table upload regression now verifies that the page-table-sized
  GPU copy is absent before the explicit resolve stage runs and present after
  it. Focused coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`
  - `LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualTryGetDoesNotAutoResolveCurrentFrame`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
  all completed successfully. Status remains `in_progress`: the explicit
  resolve stage now owns live CPU-side preparation ordering and the
  getter-driven hidden resolve side effects are gone, but final GPU allocation
  / eviction / page-table ownership and the end-to-end resolve-owned raster
  contract are still not complete. The short smoke no longer emitted the old
  `VSM_GLITCH` marker, but it still showed early startup frames where
  virtual-page raster had resolved pages before prepared draw metadata was
  ready, so that startup ordering gap also remains open.
- **March 12, 2026**: Tightened step-4 ownership so `MarkRendered` is no
  longer a backdoor current-frame resolve path. Current-frame allocation /
  eviction / page-table mutation remain explicit resolve-stage work only;
  `MarkRendered` now just transitions already-resolved pending pages to clean
  residency after raster. The cached-vs-reused atlas-debug regression was
  updated to resolve the first frame explicitly before marking it rendered,
  and new focused coverage now asserts that calling
  `MarkVirtualRasterExecuted` alone does not materialize current-frame VSM
  state. Focused coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualMarkRenderedDoesNotAutoResolveCurrentFrame`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualAtlasDebugSeparatesCachedFromReused`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualMarkRenderedDoesNotAutoResolveCurrentFrame`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualAtlasDebugSeparatesCachedFromReused`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
  all completed successfully. Status remains `in_progress`: the focused VSM
  slice is green at `42/42`, but allocation / eviction / page-table ownership
  are still CPU-authored.
- **March 12, 2026**: Closed the startup ordering gap in the live directional
  VSM path. `VirtualShadowRequestPass` and `VirtualShadowResolvePass` now skip
  startup / transition frames until the current scene view has live prepared
  draw metadata and non-empty partitions, so request generation, current-frame
  residency resolve, and resolved-page raster publication no longer run ahead
  of scene readiness. Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
  all completed successfully. The short smoke no longer showed frames with
  resolved virtual pages while `draw_bytes=0` / `partitions=0`; the first live
  resolve dispatch now begins on frame 6, when the prepared frame is ready.
  Status remains `in_progress`: allocation / eviction / page-table ownership
  are still CPU-authored and the final resolve-owned raster path is still not
  complete.
- **March 12, 2026**: Removed the remaining legacy directional VSM raster
  contract. `VirtualShadowRenderPlan` now exports only `resolved_pages`, and
  the provisional alias path is gone from code and focused tests. The
  explicit resolve stage remains CPU-authored, but raster consumers now read
  one authoritative page schedule only. Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
  all completed successfully. Status remains `in_progress`: live interactive
  `RenderScene` / Sponza validation was still pending at this point.
- **March 12, 2026**: User-reported manual step-6 validation confirms
  directional `virtual-only` is functionally stable in `RenderScene` and
  Sponza. This closes the frozen execution-plan validation step. Status
  remains `in_progress` for the `Directional VSM Production Candidate`
  milestone until edge-quality parity with the conventional directional path is
  explicitly signed off, but the frozen six-step execution plan is now fully
  landed.
- **March 12, 2026**: Completed frozen task 5 for directional VSM hardening.
  The allocator now prefers evicting invalid resident pages before unrelated
  clean cached pages when the low-tier physical pool is under pressure. Added
  focused regression coverage:
  `LightManagerTest.ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`,
  which fills the low-tier pool, moves a large caster to create pressure, and
  proves an excluded clean tracked page for a still-visible caster survives
  that pressure without reraster. Combined with the existing clipmap-shift,
  accepted-feedback motion, snap-shift, and depth-remap regressions, the
  frozen step-5 coverage set is now in place for camera motion, caster
  persistence, snap shifts, depth remap, and budget pressure. Validation
  evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
  all completed successfully. Status remains `in_progress`: frozen step 6
  still requires live interactive `RenderScene` / Sponza validation before the
  directional production-candidate gate can close.
- **March 12, 2026**: Completed frozen task 4 for the explicit resolve-owned
  CPU path. Publish-time VSM view-state construction no longer carries
  resident pages or releases physical tiles; it snapshots the authoritative
  prior resident pages, page table, metadata, and caster bounds into pending
  resolve state, and `ResolvePendingPageResidency` now performs current-frame
  carry, dirty marking, release, allocation, eviction, and page-table
  mutation as one stage. That snapshot also survives an unresolved republish
  so returning to the last resolved address space reuses the original clean
  pages instead of reallocating them. Added focused regression coverage:
  `LightManagerTest.ShadowManagerPublishForView_VirtualUnresolvedRepublishRetainsAuthoritativeResidentSnapshot`.
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualUnresolvedRepublishRetainsAuthoritativeResidentSnapshot`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`,
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
  all completed successfully. Status remains `in_progress`: frozen steps 5-6
  are still open, and the broader GPU-owned residency resolve/update path is
  still not implemented.
- **March 12, 2026**: Added the first live resolve-to-raster bridge slice for
  directional VSM. The runtime now runs `VirtualShadowResolvePass` between the
  request and raster passes, compacts currently mapped requested pages into
  per-view GPU schedule buffers, readbacks a CPU-visible
  `VirtualShadowResolvedRasterSchedule`, and lets the backend prune fine
  pending raster jobs when a compatible resolved schedule exists while always
  preserving the coarse backbone fallback. That pruning contract was later
  corrected and disabled until resolve owns raster scheduling. Added focused LightManager
  coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleDoesNotSuppressCpuPendingJobsYet`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleRejectsIncompatibleAddressSpace`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleDoesNotSuppressCpuPendingJobsYet`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleRejectsIncompatibleAddressSpace`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`,
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  and
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 40 --fps 100 --directional-shadows virtual-only`
  all completed successfully. The `RenderScene` log showed the live resolve
  pass producing compact schedules (`scheduled_pages=91`) once feedback
  stabilized, but that capture did not yet hit a reraster frame where the new
  prune path reduced executed jobs. Status remains `in_progress`: residency
  allocation is still CPU-owned and the final resolve-owned raster contract is
  not complete.
- **March 12, 2026**: Added the first backend-private GPU residency bridge
  resources for directional VSM. The runtime now builds a deterministic
  resident-page snapshot plus resolve counters, stages both into persistent
  per-view GPU buffers with stable bindless slots, and publishes that bridge
  state through VSM introspection. Added focused LightManager coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStateSnapshotTracksResidentPagesDeterministically`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStateUsesStablePerViewGpuBuffers`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolveStateSnapshotTracksResidentPagesDeterministically`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolveStateUsesStablePerViewGpuBuffers`,
  and
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  all passed. Status remains `in_progress`: the live resolve/update pass and
  resolve-owned raster scheduling are still not implemented.
- **March 11, 2026**: Corrected the remaining directional VSM scope. The
  previous wording that framed the next item as only the "final GPU request
  dedup / residency resolve / allocation pass" was incomplete because live
  rasterization still depended on a provisional CPU-authored raster-job list
  and CPU-built per-page view constants. The remaining work is now explicitly
  staged as GPU request/residency state, GPU resolve/page-table update,
  resolve-owned raster scheduling, raster-pass contract convergence, then the
  remaining invalidation/eviction/large-scene hardening. Status remains
  `in_progress`; this is a design/plan correction only.
- **March 11, 2026**: Moved the directional VSM shader-visible page table to
  persistent per-view GPU ownership while keeping residency resolve/allocation
  CPU-driven for now. The current slice now:
  - publishes a stable bindless page-table slot across republishes of the same
    view
  - stages current page-table contents into a persistent GPU buffer during the
    virtual shadow pipeline
  - marks valid mapped entries with the documented `requested this frame` bit
  Added focused LightManager coverage:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualPageTablePublicationUsesStablePerViewGpuBuffer`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualPageTableMarksMappedPagesRequestedThisFrame`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualPageTablePublicationUsesStablePerViewGpuBuffer`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualPageTableMarksMappedPagesRequestedThisFrame`,
  and
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  all passed. Status remains `in_progress`: GPU page-table ownership/update is
  now in place, but final GPU request dedup / residency resolve / allocation
  and broader runtime validation are still unfinished.
- **March 11, 2026**: Added focused non-regression coverage for the
  directional VSM snap-boundary dropout fix. The LightManager VSM suite now
  includes:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualFeedbackAddressSpaceTracksSnappedXYTranslationButIgnoresZPullback`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualIncompatibleFeedbackRebootsReceiverBootstrap`
  Validation evidence:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`,
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualFeedbackAddressSpaceTracksSnappedXYTranslationButIgnoresZPullback`,
  and
  `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualIncompatibleFeedbackRebootsReceiverBootstrap`
  all passed. Status remains `in_progress`: the focused LightManager VSM slice
  is now green, but the final GPU residency-resolve/update path and broader
  runtime validation are still unfinished.
- **March 11, 2026**: Implemented the first runtime realignment for the
  directional VSM churn diagnosed from low-fps `RenderScene` logs. The
  directional address-space comparator now preserves snapped XY light-view
  translation and ignores only Z pull-back padding, which stops stale
  feedback from surviving across a camera-driven lattice shift while still
  allowing reuse along the light ray. The coarse fallback backbone now clamps
  its frustum depth span to the visible receiver depth range when receiver
  bounds are available.
  Validation evidence:
  `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  passed `29/29`, the full `Oxygen.Renderer.LightManager.Tests.exe` run passed
  `41/41`, `oxygen-examples-renderscene.vcxproj` rebuilt successfully, and
  `Oxygen.Examples.RenderScene.exe --frames 12 --fps 3 --directional-shadows virtual-only`
  produced a short `INFO` log capture showing steady-state directional demand
  drop from the prior `selected=2275 coarse=1788` baseline to `selected=601
  coarse=48`. Status remains `in_progress` until live camera-motion captures
  confirm the atlas no longer full-wipes during translation/rotation.
- **March 11, 2026**: Corrected the active VSM troubleshooting scope after
  low-fps `RenderScene` log analysis. The atlas-inspector state split was
  necessary, but not sufficient: runtime evidence showed two deeper faults in
  the directional VSM cache/request path. First, the directional
  address-space/feedback comparator was still zeroing XY light-view
  translation, so camera motion across a snap boundary could keep
  `address_space_compatible=true` even after the absolute page lattice had
  changed. Second, the
  coarse fallback backbone was derived from the full camera frustum out to the
  projection far plane, which caused a tiny scene to request thousands of
  coarse pages and keep every resident tile marked as used. Status remains
  `in_progress` until the comparator and coarse-selection fixes are implemented
  and validated with focused VSM tests plus another short low-fps `RenderScene`
  log run.
- **March 11, 2026**: Reworked the `RenderScene` VSM atlas inspector to use
  actual backend-exported physical-tile state instead of depth-only inference.
  `VirtualShadowMapBackend` now exports one atlas-tile state per physical page
  (`cleared`, `reused`, `rewritten`), `VirtualShadowAtlasDebugPass` uploads that
  state into a structured SRV, and `VirtualShadowAtlasDebug.hlsl` colors page
  borders from the real frame classification. Validation evidence:
  `oxygen-graphics-direct3d12_shaders.vcxproj` built, `26/26` VSM-focused
  LightManager tests passed, `38/38` LightManager tests passed,
  `oxygen-examples-renderscene.vcxproj` rebuilt successfully with
  `/p:UseMultiToolTask=false /p:CL_MPCount=1`, and
  `Oxygen.Examples.RenderScene.exe --frames 4 --fps 3 --directional-shadows virtual-only`
  exited cleanly. Status remains `in_progress` until live atlas captures confirm
  whether motion is dominated by `rewritten` borders or true reuse.
- **March 11, 2026**: Fixed a `VirtualShadowAtlasDebugPass` constant-buffer
  packing bug that collapsed the atlas inspector vertically. The pass constants
  now include explicit padding between the three bindless indices and
  `atlas_dimensions`, with compile-time offset assertions in
  `VirtualShadowAtlasDebugPass.cpp`, and the HLSL constant buffer matches that
  layout. Validation evidence: the shader target rebuilt successfully and
  `Oxygen.Examples.RenderScene.exe --frames 4 --fps 3 --directional-shadows virtual-only`
  exited cleanly after the change. Status remains `in_progress` until the live
  inspector image is visually confirmed.
- **March 11, 2026**: Adjusted the atlas-inspector legend colors to improve
  state readability during camera motion. `rewritten` borders are now pure
  yellow, `reused` stays green, and `cleared` borders use a stronger blue so
  they are distinguishable from both occupied depth fill and rewritten tiles.
  Validation evidence: the shader target rebuilt successfully and
  `Oxygen.Examples.RenderScene.exe --frames 4 --fps 3 --directional-shadows virtual-only`
  exited cleanly after the change. Status remains `in_progress` until live
  captures confirm the legend is readable in motion.
- **March 11, 2026**: Corrected the atlas-inspector semantics so stable green
  no longer means "resident anywhere in the cache". The backend now separates
  `cleared`, `cached-but-unrequested`, `requested-and-reused`, and `rewritten`
  physical tiles. This makes stable frames show only actually requested reused
  pages in green, while stale carried cache lines are shown separately.
  Validation evidence: renderer tests and a short `RenderScene` smoke run are
  required after the implementation change. Status remains `in_progress` until
  that evidence is recorded.
- **March 11, 2026**: Runtime validation expanded again after the low-fps
  `RenderScene` two-cube capture showed that reduced atlas churn did not imply
  stable shadows under camera motion. The current accepted-feedback path drops
  current-frame fine receiver seeding to zero as soon as delayed request
  feedback becomes available, which is too aggressive for motion: the cache
  reuse improves, but fine-page demand becomes temporally stale and visible
  shading can pop or degrade while moving. The corrected scope is now:
  accepted feedback must remain the primary sparse signal, but the backend
  also needs a bounded current-frame fine reinforcement path that does not
  explode back into the old dense receiver bootstrap. Validation evidence so
  far: `out/build-vs/vsm-two-cubes-runtime-20260311.txt` confirms the simple
  scene settles to sparse accepted feedback (`selected=601`, `receiver_bootstrap=0`)
  after warm-up, so the remaining gap is motion stability rather than total
  cold-frame churn. Status remains `in_progress` until that hybrid path is
  implemented and revalidated.
- **March 11, 2026**: Implemented the first runtime-stability correction for
  accepted feedback. `VirtualShadowMapBackend` no longer treats
  `feedback accepted` as `current-frame fine receivers = zero`. Instead it keeps
  feedback as the primary sparse signal and adds a tightly clamped
  current-frame reinforcement band on the nearest fine clips. New diagnostics
  separate `receiver_bootstrap_pages` from
  `current_frame_reinforcement_pages`, and the focused regression now verifies that a
  translated clip shift under accepted feedback keeps bootstrap at zero while
  still adding bounded current-frame reinforcement. Validation evidence:
  `Oxygen.Renderer.LightManager.Tests.vcxproj` rebuilt,
  `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  passed `29/29`,
  `Oxygen.Renderer.LightManager.Tests.exe` passed `41/41`,
  `oxygen-examples-renderscene.vcxproj` rebuilt, and
  `out/build-vs/vsm-two-cubes-runtime-20260311-hybrid.txt` now shows steady
  accepted-feedback frames with `receiver_bootstrap=0`,
  `receiver_reinforce=51`, `selected=652`, and `rerasterized=0`. Status
  remains `in_progress` until live camera-motion validation confirms this
  removes the visible instability instead of only improving the static settle
  state.
- **March 11, 2026**: Motion validation uncovered a flaw in that first
  reinforcement pass: reinforcing around receiver-bound centers is not a good
  approximation for large receivers like the floor. It can preserve pages on
  visible caster surfaces while still missing the actual current-frame floor
  shadow footprint, which matches the reported "partial" shadows and shadows
  appearing on top faces. The corrected scope is narrower and more rendering-
  faithful: accepted feedback should be complemented by a bounded current-frame
  frustum reinforcement on the nearest fine clips, not by object-center
  reinforcement. Status remains `in_progress` until that correction is
  implemented and revalidated.
- **March 11, 2026**: Low-fps `RenderScene` validation showed that the
  frustum-reinforcement correction was itself too aggressive. In the two-cube
  static scene, accepted-feedback frames expanded to `selected=9770` and
  `current_reinforce=9122`, causing allocator pressure (`allocated=1531`,
  `evicted=1531`, `alloc_failures=8234`) even though only two small casters and
  a tiny receiver area were on screen. That means reinforcing whole fine-frustum
  regions is not viable. The corrected scope is now: keep accepted feedback as
  the primary sparse signal and reinforce only the newly exposed fine-page
  delta bands between the previous and current view regions. Status remains
  `in_progress` until that narrower delta-band path is implemented and
  revalidated with focused tests plus another low-fps runtime capture.
- **March 11, 2026**: Replaced the accepted-feedback whole-frustum
  reinforcement with absolute frustum-delta reinforcement. `VirtualShadowMapBackend`
  now stores per-clip frustum regions in absolute resident-page space and, when
  delayed request feedback is accepted, reinforces only the newly exposed
  fine-page delta bands between the previous and current view regions. The
  delta path is capped per clip so a bad comparison cannot silently explode
  back into whole-frustum fine selection. Validation evidence:
  `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  passed `29/29`,
  `Oxygen.Renderer.LightManager.Tests.exe` passed `41/41`, and
  `out/build-vs/vsm-two-cubes-runtime-20260311-delta.txt` shows accepted-feedback
  frames at `selected=778`, `current_reinforce=169`, `alloc_failures=0`, and
  steady reuse (`reused=778`, `rerasterized=0`) after warm-up instead of the
  previous `selected=9770/current_reinforce=9122` regression. Status remains
  `in_progress` until live camera-motion validation confirms the visible
  shadow instability is gone as well as the allocator churn.
- **March 11, 2026**: Live motion feedback uncovered a deeper flaw in that
  frustum-delta reinforcement: it was comparing the current view region
  against the immediately previous frame, while the accepted page seed was
  still coming from an older request-feedback source frame. Under motion, that
  means the patch band can cover only the last frame's movement even though
  the reused page set is several frames older. The corrected scope is to store
  the feedback source-frame frustum regions alongside the accepted feedback and
  compute the current reinforcement band against that source region, not the
  previous publication. Status remains `in_progress` until that baseline fix is
  implemented and revalidated.
- **March 11, 2026**: Runtime troubleshooting still shows partially rendered
  or misplaced directional shadows after the cache/request optimization even
  when allocator churn is reduced. The newly identified scope gap is cached
  page-content validity: the directional reuse gate currently ignores the
  depth-normalization terms (`bias_reserved.x` / effective light-space depth
  basis) when deciding whether an old page can stay clean. That can preserve
  the page cache while reusing depth contents generated under a different
  light-space depth mapping, which directly matches the remaining symptoms
  (partial shadows, detached shadows, and self-shadowing on top faces). The
  corrected next step is to tighten content reuse so that address-space reuse
  remains broad, but clean page reuse is invalidated whenever the directional
  depth mapping changes. Status remains `in_progress` until that fix is
  implemented and validated with focused integration coverage plus runtime
  evidence.
- **March 11, 2026**: Tightened directional clean-page reuse so cached page
  contents are no longer treated as valid when the light-space depth basis
  changes. `VirtualShadowMapBackend` now distinguishes directional address-space
  compatibility from directional content compatibility: address-space reuse
  still ignores page-aligned XY lattice motion, but content reuse preserves the
  light-view Z translation and the depth-mapping terms needed by shader-side
  VSM comparisons. Validation evidence:
  `ShadowManagerPublishForView_VirtualInvalidatesCleanPagesWhenDepthMappingChanges`
  passed, the full focused VSM slice passed `31/31`, the full
  `Oxygen.Renderer.LightManager.Tests.exe` run passed `43/43`, and a low-fps
  runtime smoke run completed successfully via
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 12 --fps 3 --directional-shadows virtual-only`
  with log capture in
  `out/build-vs/vsm-runtime-20260311-depth-reuse-gate.txt`. Overall status
  remains `in_progress` because interactive visual validation of the reported
  motion artifacts still has to be confirmed in the live demo.
- **March 19, 2026**: Completed the next directional VSM parity slice on the
  live scene request path. `VirtualShadowRequest.hlsl` now reconstructs a
  conservative screen-space geometric normal from the depth buffer and skips
  clearly backfacing directional pixels during page marking, matching UE's
  page-marking design intent more closely than the previous "request every
  visible depth pixel" behavior. Validation on the unchanged live scene:
  `current-live-vsmresolve-requestcull-20260319.bmp` stayed overwhelmingly
  green at the then-current `8192`-tile pool, and the same live camera also
  stayed stable after reducing Ultra physical tile capacity back down to UE-size
  parity territory:
  `current-live-vsmresolve-requestcull-6144-20260319.bmp`,
  `current-live-vsmresolve-requestcull-4096-20260319.bmp`, and
  `current-live-vsmresolve-requestcull-2048-20260319.bmp` all remained green,
  while `current-live-virtual-requestcull-2048-20260319.bmp` and
  `current-live-vsmcompare-requestcull-2048-20260319.bmp` remained visually
  stable on the same unchanged live scene. The `2048`-tile run logged
  `physical_tiles=2048` in
  `current-live-vsmresolve-requestcull-2048-20260319.stderr.log`. Status
  remains `in_progress` because this slice is validated on the current live
  camera only, and the page-marking cull is still conservative rather than
  using UE's exact directional source-radius threshold.

- **March 22, 2026**: Corrected the directional VSM request-cull scope after
  the March 19 implementation regressed page-resolution stability under close
  camera motion. Root cause: the screen-space geometric-normal test was allowed
  to skip the selected base page entirely, so view-dependent depth-neighborhood
  noise at silhouettes and disocclusions could suppress the authoritative
  request bit and make valid pages appear unresolved. The fix keeps the base
  page request authoritative and uses the reconstructed normal only to suppress
  optional local border dilation on high-confidence neighborhoods with four
  valid centered neighbors and bounded span disparity. Ultra physical tile
  capacity remains capped at `2048`, and follow-up live validation confirmed
  that the request-cull fix restores stable close-camera page resolution at
  that budget. Status remains `in_progress` until page-demand reduction versus
  the uncapped `8192` pool is quantified on the target heavy scenes.

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
