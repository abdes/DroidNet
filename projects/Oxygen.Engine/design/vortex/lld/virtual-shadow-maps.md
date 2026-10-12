# Virtual Shadow Maps LLD

**Phase:** post-baseline ShadowService strategy
**Deliverable:** [VX-SHADOW-01](../milestones/VX-SHADOW-01/README.md)
**Status:** `planned` — decisions approved; starts after camera-relative
rendering (VX-VIEW-03) and the temporal resolve (VX-POST-02).

## Summary

Virtual shadow maps (VSM) are a second `ShadowService` strategy beside the
conventional maps in [shadow-service.md](shadow-service.md). Each shadow-casting
light owns one or more 16k × 16k virtual depth maps split into 128-texel pages.
Only pages that a visible receiver needs are backed by a page in a shared
physical pool, rendered, and cached until the scene invalidates them.

The design follows UE5.7's GPU-driven model (§1.3):

- The GPU marks, reuses, allocates, initializes, rasterizes and samples pages
  in the same queue order. The CPU never reads page state back to decide
  anything. Page requests marked in a view are mapped in that view's frame.
- The CPU owns light-level policy only: which lights use VSM, their clipmap
  and local layouts, projection data, cache identity, light-level
  invalidation and the caster change list.
- Every directional clipmap level is its own virtual map with one mip.
  Local-light maps carry a full mip chain. An unmapped page falls back to the
  nearest coarser mapped page through the page table, so sampling never
  reads unrendered memory.
- The physical pool is an `R32_UINT` texture array. Casters rasterize into
  virtual space without render targets; the pixel shader resolves the page
  and writes with `InterlockedMax` (reversed-Z).
- Opaque receivers get shadow-map ray tracing (SMRT) from a projection pass
  at the end of Stage 8, as in UE5.7: one screen mask per directional light
  and one packed mask for local lights. Forward and deferred opaque lighting
  read them. Translucency, volumetric fog and future water and hair sample
  the VSM directly through one shared helper. Each light keeps its own
  visibility; nothing is multiplied across lights.
- Consumers find a light's VSM through `LightShadowReference` with
  `projection_kind = Virtual`.

The previous VSM work (retired `Oxygen.Renderer` plus the shader tree under
`Services/Shadows/Vsm/`) is reviewed in
[VX-SHADOW-01 findings](../milestones/VX-SHADOW-01/review.md). That review
explains each change from the earlier design.

## 1. Scope and Context

### 1.1 What This Covers

- VSM for directional, spot and point lights selected by the renderer shadow
  method (§2, VSM-D2).
- Per-view page marking from Stage 3 depth, GPU page management, caster
  rasterization, page caching and invalidation.
- SMRT projection for opaque receivers and direct sampling for every other
  receiver.
- Publication to every existing shadow consumer through the
  [shadow association ABI](lighting-gpu-abi.md#shadow-association-and-deferred-draws).
- Diagnostics: page-state visualization, pool pressure and overflow feedback.

Not covered here: shadow transmission through translucent and subsurface
receivers (VX-SHADOW-03) and
geometry virtualization.
Contact shadows keep their own product and are multiplied with VSM
visibility exactly as with conventional maps.

### 1.2 Stage Position

VSM runs entirely inside Stage 8 (`ShadowService::RenderShadowDepths`), which
already runs once per scene view inside the SceneRenderer view loop. Each call
is one **VSM epoch** (§7).

| Position | Stage                     | VSM use                                                                      |
| -------- | ------------------------- | ---------------------------------------------------------------------------- |
| Input    | Stage 3 depth prepass     | Complete opaque and masked scene depth for page marking                      |
| Input    | Stage 8 receiver inputs   | Translucent draw bounds for page marking                                     |
| Input    | Stage 6 light grid        | Local-light pruning during page marking                                      |
| **This** | **Stage 8 shadow depths** | Epoch: setup, invalidate, mark, manage, initialize, raster, project, publish |
| Consumer | Stage 9 forward branch    | Opaque SMRT masks                                                            |
| Consumer | Stage 12 deferred direct  | Opaque SMRT masks                                                            |
| Consumer | Stage 14 volumetric fog   | Direct sampling of coarse pages                                              |
| Consumer | Stage 18 translucency     | Direct sampling of pages marked from its bounds; coarse-page fallback        |

### 1.2.1 Receiver Demand Contract

Page marking needs every receiver that will sample a VSM, and every sampler
must run after rasterization. UE5.7 satisfies this with one aggregation
point: the water depth prepass (before or after the base pass, by policy),
froxels created with the occlusion/HZB pass, the base pass, hair visibility
and a front-layer translucency depth pass all run before
`BeginMarkVirtualShadowMapPages`, and every consumer runs after
`RenderShadowDepthMaps` (`DeferredShadingRenderer.cpp:2624-2633, 2785-2788,
3002-3003, 3065-3131`). UE places that point after its base pass because its prepass can
be partial (Nanite) and its marking reads GBuffer shading models. As a
result UE supports VSM neither with forward shading (`:2819-2824`) nor with
early shadow rendering (`:2843-2852`).

Vortex keeps the same contract with the aggregation point at Stage 8:

- **One aggregation point.** All VSM page marking, management and raster
  run in Stage 8. Every VSM consumer runs after Stage 8.
- **Receiver producers precede Stage 8.** Every receiver family publishes a
  demand product before Stage 8. Each product needs only complete opaque
  depth, which Stage 3 provides, so none depends on the base pass.
- **No silent gaps.** A receiver family without a demand product samples
  only coarse pages and its LLD must say so. A family cannot ship full-
  resolution VSM shadows without adding its producer row below.

| Receiver family                       | Demand product                                | Producer and stage                     | UE5.7 equivalent                                      |
| ------------------------------------- | --------------------------------------------- | -------------------------------------- | ----------------------------------------------------- |
| Opaque and masked surfaces            | Scene depth and a receiver stencil bit (§5.3) | DepthPrepass, Stage 3                  | Scene depth and GBuffer shading model                 |
| Volumetric fog, heterogeneous volumes | Occupied froxel list                          | ScreenHzb/Occlusion, Stage 5           | `Froxel::FRenderer` beside the HZB pass               |
| Single-layer water                    | Water surface depth                           | Water depth prepass in Stage 3         | `RenderSingleLayerWaterDepthPrepass` (before marking) |
| Front-layer translucency              | Front translucent surface depth               | Translucency depth prepass in Stage 3  | `RenderFrontLayerTranslucency` for marking            |
| Hair                                  | Hair visibility depth                         | Hair visibility prepass before Stage 8 | Hair visibility before marking                        |
| Other translucency                    | Translucent draw bounds (§5.3)                | Receiver inputs, Stage 8               | None: coarse pages                                    |
| Particles                             | None: coarse pages                            | VSM coarse marking, Stage 8            | Coarse pages                                          |

VX-SHADOW-01 delivers the opaque, volumetric-fog, translucent-bounds and
coarse rows; volumetric fog samples coarse pages until its froxel slice
lands ([abdes/DroidNet#57](https://github.com/abdes/DroidNet/issues/57)).
The other rows belong to their families' future milestones. Their prepasses
are Stage 3 sub-passes, which the stage contract allows.

A view without Stage 3 depth (wireframe-only and unlit views) shades no
lit receiver and marks no pages. Every lit view runs the depth prepass, and
the prepass reports itself incomplete only for a view with nothing to draw.

What Vortex gains over UE's placement: VSM works in the forward render mode
(Stage 9 forward shading consumes it), and the SMRT projection (§5.11) runs
before the base pass for both modes. What it costs: every new receiver family
must build a depth-only prepass before Stage 8 rather than reuse later
results. Those prepasses are the same work UE does for water, front-layer
translucency and hair; only their position differs.

GBuffer-based marking filters become a Stage 3 output: the depth prepass
writes a scene-stencil receiver bit from the instance's resolved Receive
Shadows ([editor-rendering.md §4](editor-rendering.md#4-conventional-shadows-and-receiver-control)),
so marking skips non-receivers as UE does. The prepass writes it through the
stencil reference of its raster segments
([depth-prepass.md §4](depth-prepass.md#4-execution-contract)).

### 1.3 Authority

Architecture: [ARCHITECTURE.md](../ARCHITECTURE.md) §6.2 Stage 8 and §8
service ownership. Shadow consumers: [lighting-gpu-abi.md](lighting-gpu-abi.md).
Culling machinery: [occlusion.md](occlusion.md). Upload and frame rings:
[upload.md](upload.md). Non-blocking statistics:
[gpu-feedback.md](gpu-feedback.md).

Parity references, from the local UE5.7 tree
(`Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/` and
`Engine/Shaders/Private/VirtualShadowMaps/` unless noted):

| Concern                        | UE5.7 source                                                                            |
| ------------------------------ | --------------------------------------------------------------------------------------- |
| Constants and encodings        | `Shaders/Shared/VirtualShadowMapDefinitions.h`, `VirtualShadowMapPageAccessCommon.ush`  |
| Frame flow and page management | `VirtualShadowMapArray.cpp`, `VirtualShadowMapPhysicalPageManagement.usf`               |
| Page marking                   | `VirtualShadowMapPageMarking.usf/.ush`                                                  |
| Flags, mapped-mip propagation  | `VirtualShadowMapPageManagement.usf` (`PropagateMappedMips`)                            |
| Clipmap construction           | `VirtualShadowMapClipmap.cpp`                                                           |
| Cache, light entries, LRU      | `VirtualShadowMapCacheManager.cpp/.h`, `VirtualShadowMapCacheGPUInvalidation.usf`       |
| Non-Nanite raster              | `Shaders/Private/ShadowDepthVertexShader.usf`, `ShadowDepthPixelShader.usf`             |
| Static merge, per-page HZB     | `VirtualShadowMapPhysicalPageManagement.usf` (`MergePhysicalPixel`, `BuildHZBPerPage*`) |
| Sampling and filtering         | `VirtualShadowMapProjectionCommon.ush`, `VirtualShadowMapProjectionFilter.ush`          |

Retired `Oxygen.Renderer` VSM code is not a reference
([Oxygen rules](../../../../../design/oxygen/RULES.md#ownership)).

### 1.4 Rejected Alternatives

| Alternative                                                | Reason                                                                                                                                          |
| ---------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| CPU allocation planner fed by a readback of page requests  | Synchronous GPU→CPU wait every frame, or decisions one frame late that leave receivers without pages after camera motion.                       |
| Clipmap levels as mips of one map                          | Levels are concentric and snapped independently; `child >> 1` maps to the wrong parent. UE uses one map per level.                              |
| D32 depth pool with per-page viewports and a scratch merge | Depth targets cannot be UAVs, so merges and clears need copies; one draw per page multiplies draw count by page count.                          |
| One screen mask multiplied over all lights                 | Darkens each light with other lights' shadows; unusable by forward, translucent and fog consumers.                                              |
| Culling casters with the camera HZB                        | A caster hidden from the camera can shadow visible receivers ([occlusion.md §6.4](occlusion.md#64-shadow-views)).                               |
| Per-frame virtual IDs matched by string remap keys         | Fails across views rendered in one frame; generational cache IDs (§4.2) identify maps directly.                                                 |
| Primitive reveal tracking                                  | Vortex never culls casters by camera visibility, so a cached page already contains every caster it overlaps; LOD changes alter the fingerprint. |
| Directional-only VSM policy                                | A renderer must not mix techniques per light family without an authored reason; see VSM-D2.                                                     |

## 2. Decisions

All decisions are approved ([milestone](../milestones/VX-SHADOW-01/README.md#decisions)).

| ID      | Decision               | Proposed value                                                                                                                                                                                                                                                                                                             |
| ------- | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| VSM-D1  | Stage placement        | **Approved.** Stage 8 is the single aggregation point; every receiver family publishes a demand product before it (§1.2.1). Stage order unchanged.                                                                                                                                                                         |
| VSM-D2  | Selection              | **Approved.** `RendererConfig::shadow_method` = `kConventional` or `kVirtual`, renderer-wide, all light kinds. Replaces `DirectionalShadowImplementationPolicy`. No per-light mixing.                                                                                                                                      |
| VSM-D3  | Consumer model         | **Approved.** SMRT in this milestone, as UE: opaque projection at the end of Stage 8 into per-directional-light masks and packed local-light mask bits (§5.11); direct sampling for every other consumer (§6).                                                                                                             |
| VSM-D4  | Pool format and raster | **Approved.** `R32_UINT` `Texture2DArray` pool, render-target-less raster with `InterlockedMax` in the pixel shader.                                                                                                                                                                                                       |
| VSM-D5  | Raster submission      | **Approved.** `VsmDrawRecord` indirection: one indirect command per (caster draw, raster view); one `ExecuteIndirect` per raster-state segment for all raster views of an epoch.                                                                                                                                           |
| VSM-D6  | Static caching         | **Approved.** Two-slice data model from S1–S2 (pool slices, page flags, raster slice selection); the static slice is enabled in S9 with UE's frames-static threshold of 100.                                                                                                                                               |
| VSM-D7  | Pool budget            | **Approved.** Physical pages per `ShadowQualityTier`: Low 1024, Medium 2048, High 2048, Ultra 4096 (64 KiB each). Doubled when the static slice is on.                                                                                                                                                                     |
| VSM-D8  | `detail_geometry` flag | **Approved.** `kDetailGeometry` kept: pixel marking sets it; small casters skip pages without it and coarse local pages keep dynamic cache (§4.4).                                                                                                                                                                         |
| VSM-D9  | Distant local lights   | **Approved.** Single-page maps and the refresh budget land in slice S10, after full local maps.                                                                                                                                                                                                                            |
| VSM-D11 | SMRT noise             | **Approved.** The temporal resolve ([temporal-resolve.md](temporal-resolve.md), VX-POST-02) is delivered before VX-SHADOW-01 starts. SMRT rotates its ray sets per frame with the renderer's temporal frame index and blue noise and relies on the resolve; no interim filter or fixed-noise path.                         |
| VSM-D12 | SMRT source size       | **Approved.** Directional SMRT uses the authored `angular_size_radians`, local SMRT uses `source_radius`, as UE uses `LightSourceAngle` and `SourceRadius`. This delivers the shadow-softness part of EV01-LIGHT-FINITE-SOURCE for VSM lights; the editor label for the angular size changes with it (S6).                 |
| VSM-D13 | Numeric domain         | **Approved.** VSM follows the renderer's camera-relative convention ([camera-relative-rendering.md](camera-relative-rendering.md#7-shadows), CR-D5): each map carries its own shadow origin, casters rasterize in shadow-translated space and receivers move from the view's translated space by `O_view − O_shadow` (§3). |
| VSM-D10 | Legacy documents       | **Approved.** `design/renderer-shadows/virtual-shadow-map-*.md` are retired; this LLD and the milestone replace them ([coverage](../milestones/VX-SHADOW-01/review.md#legacy-document-coverage)).                                                                                                                          |

## 3. Constants and Coordinate Spaces

Shared by C++ and HLSL through a `vsm` file entry in the contract schema
(`Vortex/Meta/ShaderContracts.yaml`), which generates
`Vortex/Shadows/Vsm/Types/Generated.VsmContracts.h` and
`Contracts/Shadows/Generated.VsmContracts.hlsli` with their parity checks;
there are no hand-written mirrors:

| Constant                      | Value  | Meaning                                                     |
| ----------------------------- | ------ | ----------------------------------------------------------- |
| `kVsmPageSize`                | 128    | Texels per page edge                                        |
| `kVsmLevel0PagesXY`           | 128    | Pages per edge at mip 0                                     |
| `kVsmMaxMipLevels`            | 8      | Mips per local-light map (128 → 1 pages)                    |
| `kVsmVirtualResolution`       | 16384  | Texels per edge at mip 0                                    |
| `kVsmPageTableEntriesLocal`   | 21845  | Local map: Σ 4^k for k = 0..7, mips packed after each other |
| `kVsmPageTableEntriesClipmap` | 16384  | Clipmap level: mip 0 only                                   |
| `kVsmMaxPoolPagesXY`          | 128    | Pool pages per row; keeps the pool within 16384 texels      |
| `kVsmClipmapFirstLevel`       | 6      | Finest absolute clipmap level (receivers from 2^6 UE units) |
| `kVsmClipmapLastLevel`        | 22     | Absolute level of the coarsest clipmap level                |
| `kVsmCoarseLevels`            | 15..18 | Absolute clipmap levels that always receive coarse marking  |

Clipmap levels follow UE's convention (`FVirtualShadowMapClipmap::GetLevelRadius`).
A receiver at distance `d` from the clipmap origin selects absolute level
`floor(log2 d)`, so level `L` serves `[2^L, 2^(L+1))`. The level therefore
covers radius `2^(L+1)`, snaps its centre to a grid of that radius and spans
four radii: 128 pages, 32 pages per radius. Distances are in UE units
(centimetres); Oxygen converts with `d_ue = 100 · d_m`. Level counts and the
coarse window keep UE's defaults
(`r.Shadow.Virtual.Clipmap.FirstLevel/LastLevel/FirstCoarseLevel/LastCoarseLevel`).

**Numeric domain (VSM-D13).** VSM follows
[camera-relative rendering](camera-relative-rendering.md#7-shadows). Each map
has a shadow origin `O_s` chosen on the CPU in double: the snapped centre of a
clipmap level, or the light position for spot and point maps.
`shadow_translated_to_view` maps `T_s = W − O_s`, and casters are rasterized
at that precision through the shared mesh-position helper. Marking, SMRT and
direct sampling start from the receiver's translated position `T` and form
`T_s = T + (O_view − O_s)` with `DFDifferenceDemote`
([camera-relative §5.4](camera-relative-rendering.md#54-double-float-shader-library)). `clipmap_origin_offset`
is the epoch's clipmap origin relative to `O_s`, computed in double, so level
selection never forms an absolute position. Cached pages stay valid across
camera motion because page placement comes from the double-precision snapped
grid and `clipmap_corner_relative_offset`.

Depth is reversed-Z in every map: cleared to 0, larger is closer to the light.
Virtual texel `(x, y)` at mip `m` has page `(x, y) >> 7` and in-page offset
`(x, y) & 127`. Page-table index of page `p` at mip `m` in map slot `s` is
`base(s) + MipOffset(m) + p.y * (128 >> m) + p.x`. `base(s)` comes from a
per-slot table uploaded with the projections; clipmap slots take 16384
entries and local slots 21845.

## 4. Data Model

### 4.1 Virtual Maps

| Light kind  | Maps per cache entry                             | Mips         |
| ----------- | ------------------------------------------------ | ------------ |
| Directional | One per clipmap level, per (light, view)         | mip 0 only   |
| Spot        | One                                              | `min_mip`..7 |
| Point       | Six, one per cube face, native face order (§6.2) | `min_mip`..7 |

A clipmap level is a 128 × 128-page orthographic map centred on the view
origin snapped to a grid of its radius `2^(L+1)` (§3), with a depth range of
`ZRangeScale = 1000` level radii (UE default). Each level stores its
`clipmap_corner_relative_offset` (UE `ClipmapCornerRelativeOffset`): its
snapped corner relative to the coarsest level's grid, in units of a quarter
level width. A level moves in whole snap units of 32 pages; its cached pages
keep their content and the offset delta tells page management where each
page moved (§5.4). Snapping and offsets are computed in double precision on
the CPU.

### 4.2 Cache Identity

`VsmCacheId` is a `NamedType` over a generational index from Nexus
`IndexReuse`. One cache entry exists per directional (light, view) pair and
per local light. Each entry owns a contiguous run of `VsmMapId`s (clipmap
levels or cube faces), allocated with the entry and retired with it.

- Physical page metadata stores the owning `VsmMapId` and its generation.
  A generation mismatch means the owner was retired: the page is free.
- An entry not referenced by any epoch for
  `kVsmMaxLightAgeEpochs = 10` frames is retired (UE
  `MaxLightAgeSinceLastRequest`).
- Per epoch, the active maps are packed into dense **map slots**. A small
  `map_id → slot` buffer converts identities to slots for page management.
  This replaces per-frame virtual IDs and remap tables.

### 4.3 Page Table

`RWStructuredBuffer<uint>`, one region per active slot sized as in §3, rebuilt each epoch
from physical metadata. Encoding matches UE (`ShadowEncodePageTable`):

| Bits  | Field                                                             |
| ----- | ----------------------------------------------------------------- |
| 0–9   | Physical page x                                                   |
| 10–19 | Physical page y                                                   |
| 20–25 | LOD offset: 0 = mapped at this level, n = mapped n levels coarser |
| 30    | Valid for rendering: mapped at this level and uncached this epoch |
| 31    | Any LOD valid                                                     |

An entry with bit 31 clear has no usable data anywhere in its chain.

### 4.4 Page Flags

One `uint` per page-table entry, cleared each epoch:

| Bit | Name               | Writer                                           |
| --- | ------------------ | ------------------------------------------------ |
| 0   | `kRequested`       | Page marking                                     |
| 1   | `kAllocated`       | Physical page update or allocation               |
| 2   | `kDynamicUncached` | Physical page update or allocation; invalidation |
| 3   | `kStaticUncached`  | Same; static slice only (S9)                     |
| 4   | `kDetailGeometry`  | Pixel marking (not coarse marking)               |

`kDetailGeometry` follows UE (`VirtualShadowMapPageMarking.usf`,
`IsDetailGeometry`):

- A caster whose estimated footprint in the raster view is below 16 pixels
  (dynamic) or 1 pixel (static) is drawn only into pages with the bit; pages
  requested only by coarse marking skip small casters.
- Local maps carry `ForceCacheDynamicCoarse`: a dynamic invalidation reaches
  only pages with the bit, so coarse local pages keep their dynamic cache.

**Hierarchical page flags** (`HPageFlags`) are a per-map reduction of bits
1–4 over 2×2, 4×4, … page blocks, stored as a mip chain beside the flags.
Instance culling and invalidation test a projected page rectangle with at
most four reads. **Uncached page rects** (`uint4` per map slot and mip) bound
the pages that need rendering; the raster vertex shader clips to them.

### 4.5 Physical Pool and Metadata

The pool is `Texture2DArray<uint>` (`R32_UINT`), `pages_x × 128` by
`pages_y × 128`, `pages_x ≤ 128`, with one slice (dynamic) or two (dynamic,
static). Page `i` sits at `(i % pages_x, i / pages_x)`. The HZB is a separate
`R32_FLOAT` texture of half resolution with page-local mips (§5.10).

`VsmPhysicalPageMeta`, 24 bytes, one per physical page, persistent:

| Offset | Field                  | Meaning                                                                |
| -----: | ---------------------- | ---------------------------------------------------------------------- |
|      0 | `flags`                | Allocated, dynamic/static uncached, dirty, view-uncached, unreferenced |
|      4 | `last_requested_epoch` | Epoch counter of the last request (LRU key)                            |
|      8 | `map_id`               | Owning `VsmMapId` index                                                |
|     12 | `map_generation`       | Generation of the owner                                                |
|     16 | `mip_level`            | Mip within the owner map                                               |
|     20 | `page_address`         | x in bits 0–15, y in bits 16–31, in the owner's page grid              |

Shaders read and write it as a typed structured buffer; no byte offsets are
hand-coded.

### 4.6 Projection Records

`VsmProjection`, 224 bytes, one per active map slot, uploaded each epoch:

| Offset | Field                                                               |
| -----: | ------------------------------------------------------------------- |
|      0 | float4x4 `shadow_translated_to_view`                                |
|     64 | float4x4 `shadow_view_to_clip`                                      |
|    128 | float3 `shadow_origin_high`                                         |
|    140 | float `light_source_extent` (radians or metres, §5.11)              |
|    144 | float3 `shadow_origin_low`                                          |
|    156 | int `clipmap_level` (absolute)                                      |
|    160 | float3 `clipmap_origin_offset` (clipmap origin − `O_s`)             |
|    172 | uint `clipmap_levels_remaining`                                     |
|    176 | int2 `clipmap_corner_relative_offset`                               |
|    184 | uint `light_type` (Directional 0, Spot 1, PointFace 2)              |
|    188 | uint `flags` (Uncached 1, CoarseLevel 2, ForceCacheDynamicCoarse 4) |
|    192 | float `resolution_lod_bias`                                         |
|    196 | uint `min_mip_level`                                                |
|    200 | uint `map_id`, 204 uint `map_generation`                            |
|    208 | uint `selection_index`, 212 uint `cube_face`                        |
|    216 | uint2 reserved                                                      |

`Uncached` marks a map whose cached content is void this epoch (§8.1).

### 4.7 Consumer Records

`VsmLightRecord`, 48 bytes, one per VSM shadow reference:

| Offset | Field                                                     |
| -----: | --------------------------------------------------------- |
|      0 | uint `first_map_slot`                                     |
|      4 | uint `map_count` (levels or 6 faces)                      |
|      8 | uint `light_type`                                         |
|     12 | uint `selection_index`                                    |
|     16 | float `normal_bias_texels`                                |
|     20 | float `depth_bias`                                        |
|     24 | float `shadow_strength`                                   |
|     28 | uint `flags` (bit 0 directional, bit 1 local packed)      |
|     32 | uint `screen_mask_srv` (directional; invalid for local)   |
|     36 | uint `smrt_ray_count`                                     |
|     40 | uint `smrt_samples_per_ray`                               |
|     44 | float `source_extent` (radians directional, metres local) |

`VsmFrameBindings`, 64 bytes, published per view through the existing
`ViewFrameBindings::virtual_shadow_frame_slot` (offset 32, a schema field that
nothing publishes today):

| Offset | Field                                           |
| -----: | ----------------------------------------------- |
|      0 | uint `page_table_srv`                           |
|      4 | uint `physical_pool_srv`                        |
|      8 | uint `projection_srv`                           |
|     12 | uint `projection_count`                         |
|     16 | uint `light_records_srv`                        |
|     20 | uint `light_record_count`                       |
|     24 | uint `pool_pages_x`                             |
|     28 | uint `filter_quality` (0 = 1 tap, 1 = 3×3 PCF)  |
|     32 | uint2 `epoch`                                   |
|     40 | uint2 `view_generation`                         |
|     48 | float `global_resolution_lod_bias`              |
|     52 | uint `coarse_first_level`                       |
|     56 | uint `local_mask_srv` (packed local-light bits) |
|     60 | uint `packed_light_capacity` (16, at most 32)   |

`LightShadowReference.projection_kind` gains `Virtual = 4`; `record_index`
indexes `VsmLightRecord`. `coverage_state` keeps its meanings; a VSM light
that requests shadows is `Complete` even when some pages fall back to coarser
LOD. The earlier 16-byte screen-mask `VsmFrameBindings` is replaced.

## 5. Epoch Algorithm

Every VSM pass is a non-camera pass: it reads one inputs record captured at
record time, with translated matrices and origins (§3), and never binds `b1`
([shader-contracts §4.9](shader-contracts.md#49-pass-inputs-and-data-integrity)). VSM records and `VsmFrameBindings` are generated-schema entries.

One epoch per `RenderShadowDepths` call. All GPU work is recorded on the view's
graphics command list in this order. Indirect dispatch arguments are produced
on the GPU; the CPU only records.

```text
E1 CPU light setup          E8  page initialization
E2 invalidation             E9  caster culling and raster
E3 page marking             E10 static merge (S9)
E4 update physical pages    E11 per-page HZB (S9)
E5 pack available pages     E12 SMRT projection
E6 allocate new pages       E13 publication
   + hierarchical flags
E7 propagate mapped mips
```

### 5.1 E1 — CPU Light Setup

For each light in the frame-light selection with `casts_shadows` and a
contributing influence:

1. Find or create the cache entry; record this epoch.
2. Build the projection: directional clipmap levels per §4.1 (snap the view
   origin per level to its radius grid, UE `FVirtualShadowMapClipmap`), spot
   perspective map, or six point faces. The light's authored maximum shadow
   distance drops levels whose inner radius lies beyond it; its distance fade
   is applied at sampling. Cascade count, splits, distribution and
   transition width are CSM settings and do not apply to VSM.
3. Light-level invalidation (§8.1): compare light transform, type, cone,
   range, bias and resolution settings with the entry. Any change sets
   `Uncached` on the entry's maps.
4. Clipmap reuse: per level, compute the page offset from the stored corner.
   A level that moved further than its window, changed depth range or world
   texel size, or whose level count changed is `Uncached` (UE
   `FVirtualShadowMapClipmap` reuse rules).
5. Assign dense map slots, upload `VsmProjection` records, the
   `map_id → slot` table and per-slot clipmap page offsets.
6. Resolve local-light min mip from `ShadowResolutionHint` and quality tier
   into `resolution_lod_bias`.

Validate inputs here: a malformed light or a full slot table fails the view
with a `LightingPreparationFailure`, as conventional shadows do.

### 5.2 E2 — Invalidation

Input: per active map slot, the entry's pending caster changes (§8.2), each a
world bounding sphere and a scope (dynamic, static, both). One thread per
(change, map slot) projects the sphere into the map with the entry's current
projection, takes the page rectangle at each mip, and sets
`kInvalidateDynamic`/`kInvalidateStatic` on the physical pages found through
the **previous epoch's page table of that map**.

Page tables are rebuilt per epoch, so each cache entry keeps the page table
of its last epoch in a persistent per-entry buffer region (one region per
map, sized as in §3). Projections are world-anchored; a translation-only clipmap pan applies
the stored page offset. A light-level change already voided the map.

A physical page found this way may have been freed or given to another owner
since that epoch. The thread flags it only if its metadata still names the
same map, generation, mip and page address.

### 5.3 E3 — Page Marking

One thread per depth pixel of the view (Stage 3 depth, skipping
`depth == 0` and pixels without the receiver stencil bit):

1. Reconstruct the translated position `T`; per map, `T_s = T + (O_view − O_s)` (§3).
2. Directional maps of this view: compute the absolute clipmap level from the
   distance from `T_s` to `clipmap_origin_offset` plus `resolution_lod_bias` and the
   global bias (UE `GetBiasedClipmapLevel`); mark the page at that level only.
3. Local maps: walk the pixel's light-grid cluster ([lighting-service.md](lighting-service.md));
   for each VSM light, skip receivers outside its influence, pick the face for
   points, compute the mip from the projected footprint (UE
   `GetMipLevelLocal`) and mark that page.
4. Page dilation (UE `PageDilationBorderSize*` = 0.05): also mark the pages
   at the sample's page address plus and minus 0.05 page, so a sample within
   5 % of a page edge requests the neighbouring page on either side.

A second pass marks **coarse pages**, as UE's `MarkCoarsePages`: the 2×2
pages around the clipmap origin in each coarse level (absolute 15–18), and
the single page of the last mip of each local map. Coarser levels contain
finer ones, so these few pages cover the camera's surroundings out to the
coarse levels' extent. They serve translucency, fog and off-screen receivers
through the LOD fallback; in-frustum opaque receivers are already marked per
pixel. Coarse pages carry no `kDetailGeometry`.

A third pass marks **translucent receivers** from the translucent draw boxes
the conventional receiver fit already computes
(`ShadowReceiverCommon.hlsli`, through the fit inputs' `worlds_srv` and
`draw_cull_records_srv`). One thread per translucent draw intersects its box,
in `T_s`, with each map's levels or mips and marks the covered pages at the
level its distance selects, clamped to a page budget per draw. Translucent
receivers that fall outside the budget sample the LOD fallback.

A fourth pass marks **volumetric-fog froxels**, as UE5.7
`GeneratePageFlagsFromFroxels`. Stage 5, beside the screen HZB, builds the
list of occupied froxels from scene depth: the volumetric fog grid cells in
front of their tile's closest depth and inside the fog's maximum distance
(UE `Froxel::FRenderer`). Marking walks the list; per froxel center in `T_s`
it marks the directional page at the level its distance selects, biased by
the froxel footprint, and for each VSM light in the froxel's light-grid
cluster the mip its footprint selects. Froxel pages carry no
`kDetailGeometry`. A view without volumetric fog publishes an empty list.

Sampling (§6) uses the same level and mip functions with no extra bias, so
every opaque receiver samples a page it requested.

### 5.4 E4 — Update Physical Pages

One thread per physical page:

1. Free if unallocated, or if `map_generation` no longer matches the owner.
2. Map the owner to its slot. If the owner is inactive this epoch, keep the
   page cached as `unreferenced`; it stays eligible for LRU eviction.
3. For clipmaps, add the slot's page offset to `page_address`; a page that
   leaves the window is freed.
4. If the map is `Uncached`, or the page carries an invalidation for its
   slice, the page stays mapped but gains `kDynamicUncached`
   (and `kStaticUncached` for a static invalidation).
5. If its virtual page is requested, write the page-table entry, set
   `kAllocated`, refresh `last_requested_epoch` and clear `unreferenced`.
   Otherwise keep it cached as unreferenced, unless older than
   `kVsmMaxPageAgeEpochs` (UE `MaxPageAgeSinceLastRequest`, 0 = unlimited).

### 5.5 E5 — Pack Available Pages

Builds two GPU lists with a prefix scan (one 1024-thread group per 1024
pages, plus a scan of group totals, as the
[occlusion compaction](occlusion.md#33-order-preserving-compaction)):

- free pages, ascending page index
- unreferenced cached pages, oldest `last_requested_epoch` first (UE
  `AllocateViaLRU`), by an epoch-age bucket sort

The available list is the free list followed by the LRU list.

### 5.6 E6 — Allocate New Pages

One thread per page-table entry that is `kRequested` but not mapped:

1. Take the next available page with an atomic counter.
2. If none is left, count an overflow and leave the entry unmapped; E7
   gives it a coarser fallback.
3. Write the entry with bit 30 (valid for rendering) and the metadata; set
   `kAllocated | kDynamicUncached` (and `kStaticUncached` with a static slice).

Then build `HPageFlags` and the uncached page rects for every slot and mip.
Overflow is diagnostic, not failure: the view renders with coarser shadows,
and E13 reports the count through GPU feedback.

### 5.7 E7 — Propagate Mapped Mips

UE `PropagateMappedMips`, one thread per mip-0 page per slot:

- Local maps: walk mips coarse to fine; an unmapped entry below a mapped
  ancestor receives that ancestor's physical address with the mip delta as
  LOD offset.
- Clipmap levels: an unmapped level-`l` page searches coarser levels
  `l+1..`; the parent page is
  `((page - 32·c_l) + (32·c_{l+k} << k)) >> k`, where `c` is the level's
  `clipmap_corner_relative_offset` and 32 = 128 / 4. The first mapped page
  is written with LOD offset `k`.

Entries written here never have bit 30 set; raster never writes through them.

### 5.8 E8 — Page Initialization

Select pages with `kDynamicUncached` (or `kStaticUncached`) into an indirect
list. Each selected page is cleared to 0. With the static slice, a page whose
static content is still valid copies static to dynamic instead, so only
dynamic casters render. One 16×16 group per 32×32 tile, as UE's
`InitializePhysicalPagesIndirectCS`.

### 5.9 E9 — Caster Culling and Raster

**Raster views.** One raster view per (map slot, mip) with a non-empty
uncached rect. Directional slots have one; local slots up to eight.

**Candidates.** The light's caster set from `ShadowCasterDependencies`,
already filtered by light influence. Camera visibility never removes a
caster.

**Culling.** The [occlusion list builder](occlusion.md#3-indirect-lists) runs
with one culling view per raster view:

- frustum: the raster view's projection clipped to its uncached rect
- page test: the caster's projected rectangle must overlap a page with
  `kDynamicUncached` in `HPageFlags`
- occlusion: none with one slice, because an uncached page starts empty.
  With the static slice (S9), a page re-rendering dynamic casters starts
  from its static copy, and its static per-page HZB culls dynamic casters
  behind static depth in a single phase

The builder emits a `VsmDrawRecord { draw_index, raster_view }` per surviving
pair (VSM-D5). The command's `StartInstanceLocation` indexes this record; VSM
PSOs read their draw index from it. All other mesh passes keep the plain draw
index contract.

**Draw.** No render targets or depth buffer. Viewport `16384 >> mip` square
(within `D3D12_VIEWPORT_BOUNDS_MAX`). The vertex shader translates the caster by the map's shadow origin through
the shared mesh-position helper, transforms to the raster view, applies UE's mip scale and bias, and writes four
`SV_ClipDistance` planes for the uncached rect. Directional maps clamp casters
in front of the near plane to it (pancaking). The pixel shader:

```hlsl
uint entry = page_table[PageTableIndex(slot, mip, vpos.xy >> 7)];
if (entry & kVsmValidForRendering) {
  uint2 texel = DecodePhysical(entry) * 128 + (vpos.xy & 127);
  InterlockedMax(pool[uint3(texel, slice)], asuint(vpos.z));
}
```

Masked casters run their alpha test before the write
(`VortexVsmDepthMaskedPS`). Opaque casters use a pixel shader without texture
reads. Depth bias defaults to zero; VSM resolution and the receiver normal
offset (§6.3) replace slope bias.

### 5.10 E10–E11 — Static Merge and HZB (S9)

With a static slice, pages where static casters rendered this epoch merge
`dynamic = max(dynamic, static)` in place (UE `MergePhysicalPixel`).
Dirty pages then rebuild a per-page HZB of **furthest** depth (`min` in
reversed-Z) and its page-local top mips (UE `BuildHZBPerPage`,
`BuildHZBPerPageTop`). Nothing reduces across page boundaries.

### 5.11 E12 — SMRT Projection

Shadow-map ray tracing for opaque receivers, after the epoch's pages are
final. Inputs: Stage 3 depth with the receiver stencil bit, the geometric
normal reconstructed from depth, and the view's light grid. UE5.7 reference:
`VirtualShadowMapProjection.usf`, `VirtualShadowMapSMRTTemplate.ush`,
`VirtualShadowMapProjectionDirectional.ush`, `VirtualShadowMapProjectionSpot.ush`.

Per receiver pixel and light:

1. Offset the receiver along its geometric normal by
   `max(0.02, 0.0005 · d / cot(fov_x / 2))` UE units, `d` being the distance
   to the camera (UE `VirtualShadowMapGetNormalBiasLength` with
   `r.Shadow.Virtual.NormalBias` 0.5 scaled by 1/1000). Then trace a short
   screen-space
   ray toward the light (UE `ScreenRayLength` = 0.015) against Stage 3
   depth. This is the receiver bias, not the authored contact shadow, which
   stays a separate product.
2. Cast `smrt_ray_count` rays (UE default 7) from the receiver toward points
   distributed over the light source: the directional disk of
   `source_extent` radians, or the local sphere of `source_extent` metres
   (VSM-D12). Local rays span at most 0.03 rad from the light.
3. March each ray with `smrt_samples_per_ray` samples (default 8) through the
   receiver's clipmap level or mip, resolving every sample through the page
   table and its LOD fallback. Behind an occluder, extrapolate the occluder
   depth with slope at most 5 (directional) or 0.05 (local) and dither by two
   texels, as UE does; directional rays are 1.5 × the camera-scaled length.
4. Visibility is the fraction of unoccluded rays.

Outputs, both view-local transients from the scene-texture pool:

- **Directional mask.** One `R8_UNORM` screen texture per directional VSM
  light, referenced by `VsmLightRecord.screen_mask_srv`.
- **Packed local mask.** One `R32G32B32A32_UINT` screen texture. For each
  pixel, the VSM local lights of its light-grid cluster are taken in grid
  order; the `i`-th stores 4-bit visibility in bits `4i..4i+3` (UE
  `PackShadowMask`), up to `packed_light_capacity` (UE
  `OnePassProjection.MaxLightsPerPixel` = 16). A light beyond the capacity
  is sampled directly by its consumer and counted for diagnostics.

Ray sets rotate per frame from a blue-noise sequence and are resolved by the
temporal resolve (VSM-D11), as in UE5.7. There is no spatial denoise path. A
view rendered with the temporal resolve off shows the raw SMRT noise.

### 5.12 E13 — Publication

- Copy each active slot's page table into its entry's last-epoch region
  (§5.2).
- Publish `VsmFrameBindings`, the `VsmLightRecord`s and the masks for the
  view; write
  `LightShadowReference { Virtual, record }` for each VSM light.
- Record statistics (requested, allocated, reused, invalidated, overflowed
  pages; drawn casters) into the GPU feedback buffer. They are read frames
  later for diagnostics and never feed allocation.

## 6. Sampling

Opaque receivers read the E12 masks (§6.4). Every other receiver samples the
VSM directly through `Services/Shadows/Vsm/VsmSampling.hlsli`, the only
direct-sampling implementation: translucency, volumetric fog, future water
and hair, and local lights beyond the packed capacity call
`SampleVirtualShadow(light_record, translated_pos, geometric_normal)`.

### 6.1 Directional

1. Level from distance with the marking function (§5.3), without dilation.
2. Look up the entry; `LODOffset` moves to a coarser level when needed.
3. Beyond the coarsest allocated level or the authored maximum distance the
   receiver is lit, with the authored distance fade before it. An entry with
   bit 31 clear inside coverage is a marking defect, shown by the page-state
   debug mode.

### 6.2 Local

Spot: one map. Point: select the face from the dominant axis of the
receiver-to-light vector with the native cube convention used by conventional
cube maps ([lighting-gpu-abi.md](lighting-gpu-abi.md#shadow-association-and-deferred-draws)).
Mip from footprint with the marking function; apply `LODOffset`.

### 6.3 Filter and Bias

- Receiver position is offset along the geometric normal by
  `normal_bias_texels × world texel size of the sampled level or mip`.
- `filter_quality = 1`: 3×3 bilinear-weighted PCF over a 4×4 texel
  footprint in virtual texel space, comparing `asfloat` depths
  (receiver ≥ stored is lit). Taps inside one page use one page lookup and
  `GatherRed`; a footprint that crosses a page edge resolves each 2×2 quad
  through the page table, so filtering has no page seams.
- `filter_quality = 0`: one tap, for volumetric fog.
- `depth_bias` is a reversed-Z offset applied at the receiver.

### 6.4 Opaque Consumers

Forward (Stage 9) and deferred (Stage 12) lighting read, for a VSM light at
pixel `p`:

- directional: `screen_mask_srv` at `p`
- local: walk the pixel's cluster light list in grid order, counting VSM
  lights before this one to get `i`; read bits `4i..4i+3` of the packed mask
  at `p`, or sample directly when `i ≥ packed_light_capacity`

Both modes see the same masks because Stage 3 depth equals the base-pass
depth for opaque pixels. Contact shadows multiply afterwards, as today.

## 7. Multi-View Epochs

SceneRenderer renders views one at a time; Stage 8 runs once per view. Each
call is an epoch with its own marking, page table and publication.

- Directional cache entries are per (light, view). Local entries are shared
  by all views, so a later view in the frame reuses pages an earlier view
  rendered.
- The pool, metadata, last-epoch page tables and HZB are ShadowService state
  ordered by the single graphics queue. An epoch's page table, flags,
  HPageFlags and uncached rects are GPU-written, device-local per-view
  buffers (`Vortex/Internal/StructuredGpuBuffer.h`); its projections and
  light records are transient upload-ring buffers. All are referenced only by
  that view's publication.
- A view's VSM consumers all run in Stages 9–18 of the same view, before the
  next epoch on the queue. No consumer may read VSM products on another queue
  or after the next epoch; `RetainLocalContent` does not apply to VSM.
- Pool pressure across views is resolved by LRU: pages of views not in the
  current epoch are unreferenced and evicted first.

## 8. Caching and Invalidation

### 8.1 Light Level (CPU)

A cache entry's maps become `Uncached` for one epoch when: the light's
transform, kind, cone, range or shadow settings change; its clipmap level
fails reuse (§5.1); the pool is recreated; or a debug invalidation is issued.
The pool is recreated when the quality tier, page budget or slice count
changes; recreation retires every entry.

### 8.2 Caster Level (CPU feeds GPU)

Each frame, ShadowService compares caster fingerprints from
`ShadowCasterDependencies` with the previous frame:

- a new, removed or changed caster (transform, geometry, LOD, material
  masking) produces its previous and current world spheres
- a non-reusable caster (skinned, morphed, material WPO) produces its current
  sphere every frame
- with the static slice, a caster changed within the last 100 frames is
  dynamic; others are static (UE `FramesStaticThreshold`)

Each change is appended to the pending list of every live cache entry whose
light influence overlaps it (UE `CPUCullInvalidationsOutsideLightRadius`).
Lists are consumed by the entry's next epoch. A list over
`kVsmMaxPendingInvalidations = 4096` collapses to `Uncached`.

### 8.3 Correctness Rules

- A page is rendered only where its entry has bit 30 set for this epoch.
- A cached page is reused only if no invalidation reached it since it was
  rendered and its owner map is not `Uncached`.
- Every requested page is mapped in the same epoch or reported as overflow.
- Pages are rebound only through E4/E6; no consumer writes metadata.

## 9. Resources and Lifetime

| Resource                   | Format / size                                | Lifetime                        |
| -------------------------- | -------------------------------------------- | ------------------------------- |
| Physical pool              | `R32_UINT` 2D array, ≤ 16384², 1–2 slices    | Persistent, recreated on budget |
| Per-page HZB               | `R32_FLOAT`, half pool size, page-local mips | Persistent with the pool (S9)   |
| Physical metadata          | 24 B × pages                                 | Persistent with the pool        |
| Last-epoch page tables     | 16384 or 21845 × 4 B per map of a live entry | Per cache entry                 |
| Epoch page table, flags    | 16384 or 21845 × 4 B per active slot, each   | Per view, device-local          |
| HPageFlags, uncached rects | per slot and mip                             | Per view, device-local          |
| Available lists, counters  | 4 B × pages                                  | Persistent scratch              |
| Projection, light records  | 224 B, 48 B per entry                        | Transient upload ring           |
| Directional SMRT masks     | `R8_UNORM` screen size per directional light | View-local transient            |
| Packed local SMRT mask     | `R32G32B32A32_UINT` screen size              | View-local transient            |

Allocations go through the Graphics registry and count against the lighting
budget. A recreated pool, page table or record buffer gets a new resource
and descriptor; the old pair retires through `Graphics::RetireResource` once
the GPU is done with it
([descriptor lifetime](../../../src/Oxygen/Graphics/Common/README.md#descriptor-lifetime),
[upload growth](upload.md)). Growing a device-local buffer
(`StructuredGpuBuffer::Ensure`) does not copy its contents, so an epoch that
grows the metadata or a last-epoch page table runs the affected entries
`Uncached`. Map identities follow
[Nexus slot retirement](../../../src/Oxygen/Nexus/Docs/slot-retirement.md):
a retired entry is released after the last submitted epoch that used it.

## 10. Failure Modes and Diagnostics

| Condition                                  | Behavior                                                                                         |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------ |
| `shadow_method = kVirtual`, resources fail | View fails with `LightingPreparationError::kShadowStrategyUnavailable`; no conventional fallback |
| Slot or record capacity exceeded           | Same; capacities grow before the epoch when possible                                             |
| Physical pool overflow                     | Coarser LOD via E7; overflow counter, rate-limited warning                                       |
| Pending invalidation overflow              | Entry `Uncached` for one epoch; debug log                                                        |
| Invalid descriptor at dispatch             | CPU contract check before recording; never a silent shader return                                |

Shader debug modes (registered with the diagnostics service): page state per
pixel (cached, rendered this epoch, fallback LOD, unmapped), clipmap level or
mip, the physical pool with page ownership, and each light's SMRT mask.
They need working forward debug output
([abdes/DroidNet#54](https://github.com/abdes/DroidNet/issues/54)).
Statistics appear in the diagnostics panel from GPU feedback.

## 11. Source Layout

```text
src/Oxygen/Vortex/Shadows/Vsm/
|-- VsmShadowStrategy.h/.cpp        epoch orchestration called by ShadowService
|-- Internal/
|   |-- VsmCacheEntries.h/.cpp      cache identity, aging, pending invalidations
|   |-- VsmClipmap.h/.cpp           clipmap construction and reuse
|   |-- VsmLocalMaps.h/.cpp         spot and point-face projections
|   |-- VsmPhysicalPool.h/.cpp      pool, metadata, HZB resources
|   `-- VsmInvalidationQueue.h/.cpp caster fingerprint diff
|-- Passes/
|   |-- VsmPageManagementPass.h/.cpp   E2-E8 compute
|   |-- VsmRasterPass.h/.cpp           E9
|   |-- VsmPageMaintenancePass.h/.cpp  E10-E11
|   `-- VsmProjectionPass.h/.cpp       E12
`-- Types/
    `-- Generated.VsmContracts.h    <- ShaderContracts.yaml (vsm entry)

Graphics/Direct3D12/Shaders/Vortex/
|-- Contracts/Shadows/Generated.VsmContracts.hlsli
`-- Services/Shadows/Vsm/
    |-- VsmCommon.hlsli, VsmPageTable.hlsli, VsmPhysicalPageMeta.hlsli,
    |   VsmPageFlags.hlsli, VsmProjectionData.hlsli, VsmSampling.hlsli
    |-- VsmInvalidation.hlsl, VsmPageMarking.hlsl, VsmPageManagement.hlsl
    |   (UpdatePhysicalPages, PackAvailablePages, AllocateNewPages,
    |   GenerateHierarchicalFlags, PropagateMappedMips, InitializePages)
    |-- VsmRaster.hlsl              VS, opaque PS, masked PS
    |-- VsmPageMaintenance.hlsl     static merge, per-page HZB
    |-- VsmSmrt.hlsli, VsmProjection.hlsl  SMRT and mask passes
    |-- VsmMaskBits.hlsli           packed local-mask encode and decode
    `-- VsmContracts_CS.hlsl        compile-only ABI probe
```

The disposition of every existing VSM shader file is in the
[findings](../milestones/VX-SHADOW-01/review.md#shader-disposition).

## 12. Validation Gates

| Gate | Evidence                                                                                                                                                                                     |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G1   | CPU tests: clipmap snapping, reuse rules and corner offsets; cache identity, aging, retirement; fingerprint diff; light-level invalidation.                                                  |
| G2   | ABI: C++/HLSL size and offset parity for every record; GPU probe as in `LightingGpuAbiProbe.hlsl`.                                                                                           |
| G3   | GPU stage tests on synthetic depth: marking chooses one level per pixel; E4–E7 map every request, keep cached pages, apply clipmap pans, propagate fallback; overflow falls back.            |
| G4   | Raster: a known caster writes expected depths into its pages only; masked caster alpha test; no write outside the uncached rect.                                                             |
| G5   | Images: `VsmTwoCubes` and `physics_domains` match conventional shadow placement for the directional light; spot and point scenes; zero pool overflow at every tier's budget in these scenes. |
| G6   | Motion: camera rotation and pan over several frames produce no receiver sampling an unmapped page (zero fallback on opaque receivers without overflow).                                      |
| G7   | Caching: a static camera reaches zero rendered pages; a moving caster re-renders only overlapping pages; light rotation re-renders its maps.                                                 |
| G8   | Multi-view: two views with distinct clipmaps and a shared spot map render correctly in one frame.                                                                                            |
| G9   | Consumers: forward and deferred read identical masks; translucency and fog direct sampling agree with the opaque result away from penumbrae; contact shadows still apply.                    |
| G10  | Debug layer clean; RenderDoc capture analysed for one epoch; owner visual approval in the editor.                                                                                            |
| G11  | SMRT: penumbra width grows with source size and caster distance and vanishes at contact; a 20-light cluster packs 16 and samples the rest directly with no visible seam.                     |

## 13. Open Questions

- Editor exposure of `shadow_method` and quality tier (project settings
  versus renderer configuration only).
