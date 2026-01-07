# Renderer Module Implementation Plan

Living roadmap for achieving feature completeness of the Oxygen Renderer.

**Last Updated**: January 7, 2026

Cross‑References: [bindless_conventions.md](bindless_conventions.md) |
[scene_prep.md](scene_prep.md) | [shader-system.md](shader-system.md) |
[passes/design-overview.md](passes/design-overview.md) |
[lighting_overview.md](lighting_overview.md)

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
- **Lighting**: `LightManager` extracts scene lights; GPU buffers uploaded via
  transient buffers; shaders loop all lights per pixel (**no culling**)
- **Materials (basic)**: Runtime `MaterialConstants` has 6 texture slots (base
  color, normal, metallic, roughness, AO, opacity); GGX specular + Lambert
  diffuse; alpha test support; UV transform; ORM packed texture detection.
  **PAK format has full PBR**: `MaterialAssetDesc` includes emissive, clearcoat,
  transmission, sheen, specular, IOR — `MaterialLoader` reads all fields but
  `MaterialAsset` only exposes basic PBR getters. **Gap**: add getters to
  `MaterialAsset`, extend `MaterialConstants`, update `MaterialBinder`,
  implement in shaders. **Not in PAK**: height/parallax, SSS, anisotropy
- **Shader permutations (infrastructure)**: `ShaderDefine` struct, `ShaderRequest`
  with defines, `DxcShaderCompiler` per-request compilation, OXSL shader library
  format with define-keyed caching, `ComputeShaderRequestKey()` for stable cache
  identity. Passes now use `ALPHA_TEST` define for masked variants.
- **Geometry (basic)**: PAK format supports LOD (`lod_count`, `MeshDesc[]`),
  AABB per geometry/mesh/submesh, `MeshType` enum (Standard, Procedural,
  Skinned, MorphTarget, Instanced, Collision, Navigation, Billboard, Voxel).
  `GeometryAsset` stores LOD meshes; `GeometryUploader` interns GPU buffers.
  `Vertex` struct: position, normal, texcoord, tangent, bitangent, color.
  LOD selection policies exist (`FixedPolicy`, `DistancePolicy`,
  `ScreenSpaceErrorPolicy`). Per-submesh frustum culling in `ScenePrep`.
  **Gaps**: (a) `Vertex` has NO bone weights/indices — skinned meshes can't
  render, (b) No morph target storage — blend shapes not supported,
  (c) `GeometryLoader` only handles `kStandard`/`kProcedural` — skips skinned,
  (d) `DrawMetadata.instance_count` always 1 — no GPU instancing batching,
  (e) No hierarchical culling (BVH/octree) — flat per-submesh only

---

## Phase 1 – Shader Permutation Wiring ✓

Connect existing permutation infrastructure to render passes for material-driven
variants.

**Status**: Completed January 7, 2026.

### 1.1 Define Standard Material Permutation Defines ✓

- [x] Create `MaterialPermutations.h` with standard define names:
  - `HAS_EMISSIVE`, `HAS_CLEARCOAT`, `HAS_TRANSMISSION`, `HAS_HEIGHT_MAP`
  - `ALPHA_TEST`, `DOUBLE_SIDED` (replace current hardcoded PSO selection)
- [x] Document define naming convention in `shader-system.md` (§3.4)

### 1.2 Pass Permutation Integration ✓

- [x] Refactor `ShaderPass::CreatePipelineStateDesc()` to build `ShaderRequest`
      with defines from material flags
- [x] Update `DepthPrePass` similarly
- [x] Cache PSO by full `ShaderRequest` hash (already supported by
      `PipelineStateCache`)

### 1.3 Material-to-Define Mapping ✓

- [x] Map material flags to defines during draw submission (partition-based)
- [x] Ensure PSO lookup uses material-derived defines

### 1.4 Validation ✓

- [x] Test: same shader with different defines produces different PSOs
- [x] Test: identical defines reuse cached PSO

---

## Phase 2 – Emissive Materials

Add emissive support — required for any glowing objects, UI highlights, or
HDR content that benefits from bloom.

**PAK format status**: `MaterialAssetDesc` already has `emissive_texture` and
`emissive_factor[3]` (HalfFloat). No format changes needed.

### 2.1 Runtime MaterialConstants Extension

- [ ] Add `emissive_factor` (float3) to `MaterialConstants` struct
- [ ] Add `emissive_texture_index` (uint32) to texture slots
- [ ] Update HLSL `MaterialConstants.hlsli` to match (maintain 16-byte alignment)
- [ ] Ensure `sizeof(MaterialConstants)` stays root-CBV friendly
- [ ] Add `MaterialConstants::GetPermutationDefines()` method (deferred to Phase 2+)

### 2.2 MaterialAsset & MaterialBinder Integration

- [ ] Add `GetEmissiveFactor()` getter to `MaterialAsset` (PAK data already
      loaded into `desc_`, just expose it)
- [ ] Add `GetEmissiveTexture()` and `GetEmissiveTextureKey()` getters
- [ ] Extend `texture_resource_keys_` vector to include emissive slot
- [ ] Update `MaterialLoader` to populate emissive resource key
- [ ] Update `SerializeMaterialConstants()` in `MaterialBinder.cpp` to populate
      `emissive_factor` and `emissive_texture_index`

### 2.3 Shader Integration

- [ ] Update `ForwardMaterialEval.hlsli`: sample emissive texture, apply
      strength
- [ ] Update `ForwardMesh.hlsl` PS: add emissive to final output before
      tone mapping
- [ ] Ensure emissive bypasses lighting (additive, not multiplied by BRDF)

### 2.4 Validation

- [ ] Test scene with emissive-only objects (no other lighting)
- [ ] Verify emissive works with alpha-tested materials

---

## Phase 3 – GPU Instancing

Reduce draw call overhead by batching identical mesh+material combinations.

**Current state**: `DrawMetadata.instance_count` is always 1.
`RenderPass::EmitDrawRange()` calls `recorder.Draw()` per item. PAK format has
`MeshType::kInstanced` but runtime doesn't batch.

### 3.1 Instance Batching in ScenePrep

- [ ] Group `RenderItemData` by (GeometryHandle, MaterialHandle, LOD) key
- [ ] Collapse groups into single `DrawMetadata` with `instance_count > 1`
- [ ] Build per-instance transform index array for GPU lookup

### 3.2 Per-Instance Data Buffer

- [ ] Create `InstanceDataBuffer` SRV with per-instance transform indices
- [ ] Add `instance_data_buffer_srv` to `SceneConstants`
- [ ] Populate `DrawMetadata.instance_metadata_offset` for first instance

### 3.3 Shader Integration

- [ ] Update `ForwardMesh.hlsl` VS to use `SV_InstanceID`
- [ ] Fetch transform index: `instance_data[draw.instance_offset + SV_InstanceID]`
- [ ] Update `DrawMetadata.hlsli` to expose instance fields

### 3.4 Draw Call Update

- [ ] Update `RenderPass::EmitDrawRange()` to use `md.instance_count`
- [ ] Change `recorder.Draw(..., 1, ...)` to `recorder.Draw(..., md.instance_count, ...)`

### 3.5 Validation

- [ ] Test: 1000 cubes with same mesh/material render in <10 draw calls
- [ ] Verify per-instance transforms are correct

---

## Phase 4 – Clustered Light Culling

Enable Forward+ light culling so pixels evaluate only relevant lights.

### 4.1 Cluster Infrastructure

- [ ] Define `ClusterConfig` struct: grid dimensions (16×9×24), near/far,
      Z-binning scale/bias
- [ ] Add `ClusterConfig` to `EnvironmentDynamicData` (b3 CBV)
- [ ] Create `ClusterGrid` and `GlobalLightIndexList` GPU buffer types

### 4.2 Light Culling Compute Pass

- [ ] Create `LightCullingPass` class in `src/Oxygen/Renderer/Passes/`
- [ ] Implement `LightCulling.hlsl` compute shader:
  - Compute cluster AABB, intersect light spheres/cones
  - Write `{offset, count}` to `ClusterGrid`
  - Cap lights per cluster (64 max)
- [ ] Dispatch after DepthPrePass, before ShaderPass

### 4.3 ShaderPass Integration

- [ ] Update `ForwardMesh.hlsl`: compute cluster index from screen position +
      linear depth
- [ ] Replace all-lights loop with cluster-based lookup

### 4.4 Validation

- [ ] Visual test: scene with 50+ point lights
- [ ] Performance comparison: before/after

---

## Phase 5 – Post-Process Pass & Bloom

Add fullscreen post-processing with bloom to make emissive materials shine.

### 5.1 PostProcessPass Implementation

- [ ] Create `PostProcessPass` class in `src/Oxygen/Renderer/Passes/`
- [ ] Create `PostProcess.hlsl` fullscreen triangle shader
- [ ] Tone mapping: ACES filmic or Reinhard

### 5.2 Bloom Effect

- [ ] Implement brightness threshold extraction pass
- [ ] Implement separable Gaussian blur (downsample → blur → upsample chain)
- [ ] Composite bloom with tone-mapped color
- [ ] Expose bloom intensity/threshold in `GpuPostProcessParams`

### 5.3 Render Graph Integration

- [ ] Add after TransparentPass in example render graph
- [ ] Manage intermediate bloom textures

---

## Phase 6 – Transparent Sorting

Enable correct alpha blending with back-to-front ordering.

### 6.1 Depth-Based Sort Key

- [ ] Add `camera_distance` to `RenderItemData` during extraction
- [ ] Implement 64-bit sort key: `(depth << 32) | (material << 16) | mesh`

### 6.2 Transparent Partition Sorting

- [ ] Sort transparent partition by descending camera distance
- [ ] Update `PreparedSceneFrame` to expose sorted draw range

### 6.3 Documentation

- [ ] Create `passes/transparent_pass.md`

---

## Phase 7 – Shadow Mapping (Directional)

Add basic shadow support for the primary directional light.

### 7.1 Shadow Map Resources

- [ ] Create shadow map texture (2048×2048 depth)
- [ ] Allocate SRV slot in bindless heap

### 7.2 ShadowPass Implementation

- [ ] Create `ShadowPass` class: depth-only from light perspective
- [ ] Compute light-space view/projection matrices

### 7.3 Shader Integration

- [ ] Add shadow sampling to `ForwardDirectLighting.hlsli`
- [ ] PCF filtering (2×2 or 3×3)

---

## Phase 8 – Skinned Mesh Rendering

Enable skeletal animation for characters and creatures.

**Current state**: PAK format has `MeshType::kSkinned`. `Vertex` struct has NO
bone weights/indices. `GeometryLoader` skips skinned meshes. No skeleton/bone
hierarchy runtime. No animation system integration.

### 8.1 Vertex Format Extension

- [ ] Add `bone_indices` (uint4) to `Vertex` struct — up to 4 bones per vertex
- [ ] Add `bone_weights` (float4) to `Vertex` struct — normalized weights
- [ ] Update HLSL vertex input layout in `ForwardMesh.hlsl`
- [ ] Ensure backwards compatibility: detect skinned vs static by mesh type

### 8.2 Skeleton Runtime

- [ ] Define `Skeleton` class: bone hierarchy, bind poses, inverse bind matrices
- [ ] Define `SkeletonInstance` class: per-instance bone transforms
- [ ] Create bone transform buffer SRV (structured buffer of float4x3)

### 8.3 PAK Format for Skinned Meshes

- [ ] Define `SkinnedMeshInfo` union variant in `MeshDesc.info`
- [ ] Include skeleton asset reference, bone count, weights buffer reference
- [ ] Update `GeometryLoader` to handle `kSkinned` mesh type

### 8.4 Shader Integration

- [ ] Add `HAS_SKINNING` shader permutation define
- [ ] Implement vertex skinning in VS: blend position/normal by bone weights
- [ ] Fetch bone matrices from `bone_transform_buffer[bone_indices[i]]`

### 8.5 Bone Matrix Upload

- [ ] Upload bone matrices via transient buffer each frame
- [ ] Bone transform update is caller's responsibility (animation system)

---

## Phase 9 – Advanced PBR: Clear Coat

Add clear coat layer for automotive paint, wet surfaces, lacquered wood.

**PAK format status**: `MaterialAssetDesc` already has `clearcoat_texture`,
`clearcoat_normal_texture`, `clearcoat_factor` (Unorm16), and
`clearcoat_roughness` (Unorm16). No format changes needed.

### 9.1 Runtime MaterialConstants Extension

- [ ] Add `clearcoat_factor` (float, 0-1) to `MaterialConstants`
- [ ] Add `clearcoat_roughness` (float) to `MaterialConstants`
- [ ] Add `clearcoat_texture_index` (uint32)
- [ ] Add `clearcoat_normal_texture_index` (uint32)
- [ ] Update HLSL `MaterialConstants.hlsli` to match

### 9.2 MaterialAsset & MaterialBinder Integration

- [ ] Add clearcoat getters to `MaterialAsset`: `GetClearcoatFactor()`,
      `GetClearcoatRoughness()`, `GetClearcoatTexture()`,
      `GetClearcoatNormalTexture()` + resource key variants
- [ ] Extend `texture_resource_keys_` for clearcoat slots
- [ ] Update `MaterialLoader` to populate clearcoat resource keys
- [ ] Update `SerializeMaterialConstants()` to populate clearcoat fields

### 9.3 Shader Implementation

- [ ] Add second specular lobe in `ForwardPbr.hlsli`
- [ ] Attenuate base layer by clearcoat Fresnel
- [ ] Sample clearcoat normal if provided, else use base normal

---

## Phase 10 – Material Instances

Enable per-object material parameter overrides without duplicating assets.

### 10.1 Runtime Override System

- [ ] Define `MaterialOverride` struct: parameter ID → value map
- [ ] Add `SetMaterialOverride()` API to `SceneNode::Renderable`
- [ ] Store overrides in scene node, resolve during extraction

### 10.2 GPU Upload Path

- [ ] Merge base material + overrides into final `MaterialConstants`
- [ ] Cache merged constants per unique (material + overrides) combination

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
- Volumetric lighting
- Screen-space reflections
- Ambient occlusion (SSAO/GTAO)

**Advanced Geometry (require PAK/Vertex extension)**:

- Morph targets — PAK has `kMorphTarget` enum, but no `MorphTargetInfo` struct
  or morph buffer handling
- Billboard meshes — PAK has `kBillboard` enum, no runtime support
- Voxel meshes — PAK has `kVoxel` enum, no runtime support
- Collision/navigation meshes — PAK has `kCollision`/`kNavigation`, physics
  integration needed

**Advanced Materials (require PAK format extension)**:

- Height/parallax mapping — no PAK fields, high friction for visual payoff
- Subsurface scattering (skin, wax, foliage) — no PAK fields
- Anisotropy (hair, brushed metal) — no PAK fields
- Detail textures / secondary UV — no PAK fields

**Advanced Materials (already in PAK, lower priority)**:

- Transmission (glass, thin foliage) — PAK has `transmission_factor`, etc.
- Sheen (cloth, fabric) — PAK has `sheen_color_texture`, `sheen_color_factor[3]`
- Specular/IOR override — PAK has `specular_texture`, `specular_factor`, `ior`
- Decals

**Infrastructure**:

- Material shader graph / node editor
- Hot reload for materials/shaders
- Ray tracing integration
- Automated render graph resource solver

---

## Revision History

- **January 7, 2026**: Consolidated plan from 16 to 11 phases. Removed Draw
  Packet Abstraction (redundant with GPU Instancing). Moved Transmission to
  deferred (niche use case). Moved Height/Parallax to deferred (requires PAK
  changes). Converted Performance/Hardening to Ongoing section. Trimmed
  validation noise tasks.
- **January 7, 2026**: Added comprehensive geometry analysis to baseline. Found:
  PAK format supports LOD, multiple mesh types (Standard, Skinned, MorphTarget,
  etc.), but `Vertex` struct has NO bone weights/indices, `GeometryLoader` only
  handles Standard/Procedural. Added Phase 3 (GPU Instancing) and Phase 8
  (Skinned Mesh Rendering). Reorganized phases by game engine value: instancing
  moved from Phase 13 to Phase 3. Total phases now 16.
- **January 7, 2026**: Grounded all tasks in actual codebase analysis. Found:
  PAK format (`MaterialAssetDesc`) has emissive, clearcoat, transmission, sheen,
  specular, IOR fields. `MaterialLoader` reads them all. But `MaterialAsset`
  only has basic PBR getters (base_color, metalness, roughness, normal, AO).
  `MaterialConstants` runtime struct is minimal. Fixed phases 2/8/9 to include:
  (a) add getters to `MaterialAsset`, (b) extend `MaterialConstants`,
  (c) update `SerializeMaterialConstants()` in `MaterialBinder`, (d) shaders.
- **January 7, 2026**: Added Phase 1 (Shader Permutation Wiring) to connect
  existing shader compilation infrastructure to render passes. Renumbered all
  subsequent phases. Removed shader permutations from deferred list.
- **January 7, 2026**: Major update. Added material system phases (Emissive,
  Height/Parallax, Clear Coat, Transmission, Material Instances). Reordered
  phases to deliver highest-value features first. Corrected baseline to
  accurately reflect material system limitations.
- **January 7, 2026**: Initial rewrite from legacy format.
- **December 2025**: Phases 1–4 (original numbering) completed.
