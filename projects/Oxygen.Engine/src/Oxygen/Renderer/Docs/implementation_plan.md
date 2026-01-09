# Renderer Module Implementation Plan

Living roadmap for achieving feature completeness of the Oxygen Renderer.

**Last Updated**: January 10, 2026

Cross‑References: [bindless_conventions.md](bindless_conventions.md) |
[scene_prep.md](scene_prep.md) | [shader-system.md](shader-system.md) |
[passes/design-overview.md](passes/design-overview.md) |
[lighting_overview.md](lighting_overview.md) |
[override_slots.md](override_slots.md)

Legend: `[ ]` pending | `[~]` in progress | `[x]` done

---

## Current State (Baseline)

Renderer core is functional with a minimal Forward+ foundation:

- **Bindless architecture**: SM 6.6 `ResourceDescriptorHeap[]` indexing, stable
  root signature (t0 unbounded SRV, s0 sampler table, b1 SceneConstants CBV,
  b2 root constants, b3 EnvironmentDynamicData CBV)
- **ScenePrep pipeline**: Two-phase Collection → Finalization with CPU frustum
  culling and per-view `PreparedSceneFrame`
- **Render passes**: `DepthPrePass`, `ShaderPass`, `TransparentPass` with 4 PSO
  variants each (opaque/masked × single/double-sided)
- **Shader permutations**: `MaterialPermutations.h` defines standard permutation
  names (`HAS_EMISSIVE`, `ALPHA_TEST`, `DOUBLE_SIDED`). Passes build
  `ShaderRequest` with material-derived defines. PSO cached by full request hash.
- **Lighting**: `LightManager` extracts scene lights; GPU buffers uploaded via
  transient buffers; shaders loop all lights per pixel (**no culling**)
- **Materials**: Runtime `MaterialConstants` (96 bytes) with 6 texture slots
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
- [x] Extend `EnvironmentDynamicData` (b3 CBV) with cluster config fields:
      `tile_size_px`, `z_near`, `z_far`, `z_scale`, `z_bias`
      → C++: `Types/EnvironmentDynamicData.h`, HLSL: `EnvironmentDynamicData.hlsli`
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
- [x] Populate `EnvironmentDynamicData` with cluster buffer slots:
  - Created `EnvironmentDynamicDataManager` for per-view CBV buffer management
  - `Renderer::PrepareEnvironmentDynamicData()` populates cluster slots from
    `LightCullingPass::GetClusterGridSrvIndex()` and
    `LightCullingPass::GetLightIndexListSrvIndex()`
  - Copies `ClusterConfig` fields to matching CBV members
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

- [x] GPU plumbing: `EnvironmentStaticDataManager` builds bindless SRV from `SceneEnvironment`, resolves cubemap `ResourceKey` via `TextureBinder`, uploads per-frame slots, publishes `SceneConstants.bindless_env_static_slot`; `EnvironmentDynamicData` (b3) kept in sync with HLSL and per-view exposure via `Renderer::UpdateViewExposure()`.
- [x] Baseline render hooks: `SkyPass` fullscreen triangle after ShaderPass (depth-read, no clear); `SkySphere_PS/VS` consume `EnvironmentStaticData`; `ForwardMesh_PS` applies analytic fog via `AtmosphereHelpers.hlsli` with fallback sun dir when no directional light is bound.
- [x] SkyLight IBL in forward shading: sample sky cubemap for diffuse (lowest mip) and roughness-mapped specular, apply tint/intensity/diffuse/specular gains, add exposure from `EnvironmentDynamicData`.
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
     - Location: `Shaders/Passes/Atmosphere/TransmittanceLut_CS.hlsl`
     - Thread group: 8×8×1, dispatch covers 256×64 texture
     - Implements optical depth integration along view ray
     - Output: RG16F (Rayleigh + Mie extinction)
     - Reads atmosphere params from `EnvironmentStaticData`
     - Uses standard transmittance UV parameterization:
       `u = (cos_zenith + 0.15) / 1.15`, `v = sqrt(altitude / atmo_height)`

  3) [x] **Create sky-view LUT compute shader**
     - Location: `Shaders/Passes/Atmosphere/SkyViewLut_CS.hlsl`
     - Thread group: 8×8×1, dispatch covers 192×108 texture
     - Performs single-scattering raymarch with transmittance LUT sampling
     - Output: RGBA16F (inscattered radiance RGB, transmittance A)
     - UV parameterization: `u = azimuth / 2π`, `v = (cos_zenith + 1) / 2`
     - Requires sun direction from `EnvironmentDynamicData`

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
     - Apply exposure from `EnvironmentDynamicData`

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
       - Show sun direction/illuminance from `EnvironmentDynamicData`
       - Add checkbox to force analytic fallback (sets `atmosphere_flags` bit)
       - Add checkbox to visualize LUT (render LUT as overlay quad)
     - Add RenderDoc markers in compute pass for easy capture

  - [x] Bullet-proof designated sun pipeline:
    - [x] Select canonical sun on CPU: prefer `DirectionalLight::IsSunLight()` flagged light; if multiple, pick highest intensity; fallback to first directional if none flagged.
    - [x] Surface sun direction/intensity into environment data: add fields to **dynamic** payload (C++/HLSL) for sun direction (toward sun) and luminance scale, and fill them per view during ScenePrep/renderer update.
    - [x] Update `LightManager` outputs to publish the chosen sun (index or baked direction) so ScenePrep can populate environment dynamic data without shader-side heuristics.
    - [x] Consume the authoritative sun in shaders: remove “first directional light” fallback in `ForwardMesh_PS`/sky/fog paths and read the dynamic payload sun fields; keep a final hardcoded direction only if no sun is available.
    - [x] Add validation hooks: debug overlay/ImGui showing the selected sun id, direction, and intensity, and a renderdoc-friendly marker to confirm binding.
- [ ] SkyLight capture path: support `kCapturedScene` by rendering sky/background into cubemap, convolving diffuse and specular prefilter, caching BRDF LUT; fall back to `kInvalidDescriptorSlot` when unavailable.
- [x] BRDF LUT asset/binding: generate BRDF integration LUT at runtime
  (RG16F, Hammersley GGX) via `BrdfLutManager`, bind slot into
  `sky_light.brdf_lut_slot` through `EnvironmentStaticDataManager`; shaders
  sample when valid, fallback to analytic approx otherwise. LUT upload is
  async; while pending, GetOrCreateLut returns an invalid slot so consumers
  must keep the analytic fallback path.
- [ ] Volumetric clouds: consume `GpuVolumetricCloudParams` to raymarch low-res cloud color/transmittance (temporal reprojection), composite into sky before transparents, optional shadow mask for sun light.
- [ ] PostProcessVolume runtime: honor `GpuPostProcessParams` (tone mapper, bloom, exposure mode/min/max/speeds, saturation/contrast/vignette); implement luminance reduction + temporal adaptation driving `EnvironmentDynamicData.exposure`; wire bloom/tonemap once PostProcessPass (Phase 6) lands.
- [ ] Tooling + demos: hydrate environment systems in sample scenes (`SceneEnvironment` creation + SkySphere/SkyLight/PostProcessVolume defaults), ensure cubemap assets packaged and bindless slots visible in ImGui debug, add toggles to enable/disable sky/fog/exposure paths for validation.

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

- [ ] Create `PostProcessPass` class in `src/Oxygen/Renderer/Passes/`
- [ ] Create `PostProcess.hlsl` fullscreen triangle shader
- [ ] Tone mapping: ACES filmic or Reinhard

### 6.2 Bloom Effect

- [ ] Implement brightness threshold extraction pass
- [ ] Implement separable Gaussian blur (downsample → blur → upsample chain)
- [ ] Composite bloom with tone-mapped color
- [ ] Expose bloom intensity/threshold in `GpuPostProcessParams`

---

## Phase 7 – Transparent Sorting

Enable correct alpha blending with back-to-front ordering.

### 7.1 Depth-Based Sort Key

- [ ] Add `camera_distance` to `RenderItemData` during extraction
- [ ] Implement 64-bit sort key: `(depth << 32) | (material << 16) | mesh`

### 7.2 Transparent Partition Sorting

- [ ] Sort transparent partition by descending camera distance
- [ ] Update `PreparedSceneFrame` to expose sorted draw range

---

## Phase 8 – Shadow Mapping (Directional)

Add basic shadow support for the primary directional light.

### 8.1 Shadow Map Resources

- [ ] Create shadow map texture (2048×2048 depth)
- [ ] Allocate SRV slot in bindless heap

### 8.2 ShadowPass Implementation

- [ ] Create `ShadowPass` class: depth-only from light perspective
- [ ] Compute light-space view/projection matrices

### 8.3 Shader Integration

- [ ] Add shadow sampling to `ForwardDirectLighting.hlsli`
- [ ] PCF filtering (2×2 or 3×3)

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

### 10.1 MaterialConstants Extension

- [ ] Add `clearcoat_factor`, `clearcoat_roughness`, texture indices
- [ ] Update HLSL `MaterialConstants.hlsli` to match

### 10.2 MaterialAsset & MaterialBinder

- [ ] Add clearcoat getters to `MaterialAsset`
- [ ] Update `SerializeMaterialConstants()` to populate clearcoat fields

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

- [ ] Debug counters: allocations, evictions, peak usage

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
  - `density`, `height_falloff`, `height_offset`
  - `albedo_rgb`, `anisotropy_g`, `scattering_intensity`
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
- Cascade shadow maps
- Point/spot light shadows
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
