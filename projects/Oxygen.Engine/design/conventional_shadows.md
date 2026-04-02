# Conventional Cascaded Shadow Map Architecture — Oxygen Engine

Audience: renderer engineers working on Oxygen's conventional directional
shadow path

This document describes the architecture of Oxygen's conventional directional
cascaded shadow map (CSM) pipeline, including the theoretical foundations, the
Oxygen-specific architecture, each component, and their interdependencies.

---

## 1. Problem Statement

Oxygen's conventional cascaded shadow map path serves as:

- the primary shipping shadow solution for directional lights
- a reliable fallback when the virtual shadow map (VSM) path is disabled or
  unavailable
- a well-bounded GPU workload that does not scale with `O(cascades × scene)`
  on the CPU

The architecture uses GPU-driven receiver analysis and caster culling rather
than CPU-side receiver object bounding spheres, which are too coarse to produce
meaningful culling on real-world architectural scenes.

## 2. Theoretical Foundations

The design draws on four published techniques.

### 2.1 Cascaded Shadow Maps (CSM)

Reference: Microsoft Learn, *Cascaded Shadow Maps*
(`https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps`)

Reference: Zhang et al., *Parallel-Split Shadow Maps on Programmable GPUs*,
GPU Gems 3, Chapter 10
(`https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus`)

The fundamental CSM idea: partition the view frustum into depth slices
(cascades), render a separate shadow map for each cascade, and sample from the
appropriate cascade during shading. This provides high shadow resolution near
the camera without requiring a single enormous shadow map.

Key properties used by Oxygen:

- **Frustum slice geometry.** Each cascade is defined by interpolating between
  the near-plane and far-plane frustum corners using a depth partition scheme
  (practical/logarithmic split). This produces eight world-space corners per
  cascade that define the shadow camera's field of view.
- **Bounding sphere fitting.** For stability (sub-texel shadow-map panning), a
  bounding sphere is fitted to each cascade's eight corners, and the orthographic
  projection is derived from the sphere rather than the tight AABB. This
  prevents shadow swimming as the camera rotates.
- **Texel snapping.** The light-space center is snapped to texel boundaries of
  the shadow map resolution to eliminate sub-texel jitter between frames.
- **Array texture storage.** All cascades for a given light share a single
  `Texture2DArray`, with one array slice per cascade.

### 2.2 Sample Distribution Shadow Maps (SDSM)

Reference: Lauritzen, *Sample Distribution Shadow Maps*, Advances in
Real-Time Rendering, SIGGRAPH 2010
(`https://advances.realtimerendering.com/s2010/Lauritzen-SDSM%28SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course%29.pdf`)

The SDSM insight: instead of deriving cascade bounds from the view frustum
geometry alone, derive them from the actual distribution of visible depth
samples. If a cascade's frustum slice is only partially occupied by visible
geometry, the shadow map can be tightened to cover only the occupied region,
dramatically improving effective shadow resolution.

Oxygen applies this idea as follows:

- After the depth pre-pass and HZB construction, a GPU compute pass reads
  every visible depth sample.
- Each sample is classified into a cascade by comparing its linearized eye
  depth against the cascade split boundaries.
- Each sample is projected into light space using the light's rotation matrix.
- Per-cascade light-space min/max XY and depth bounds are accumulated via GPU
  atomics (using float-to-ordered-uint encoding for atomic min/max on floats).
- The resulting tight bounds per cascade are the **receiver analysis** — the
  actual light-space footprint of visible receivers.

### 2.3 Receiver-Mask-Based Caster Culling

Reference: Bittner et al., *Shadow Caster Culling for Efficient Shadow
Mapping*, I3D 2011
(`https://www.cg.tuwien.ac.at/research/publications/2011/bittner-2011-scc/`)

The idea: within each cascade's shadow map, not all tiles are occupied by
visible receivers. A caster whose light-space footprint does not overlap any
occupied receiver tile cannot produce a visible shadow — it can be safely
culled.

The technique works by:

1. Building a 2D occupancy mask in light space (tile granularity) from the
   visible receiver samples.
2. Optionally dilating the mask to account for filter footprint and depth bias.
3. Building a hierarchical (OR-reduced) version for fast broad-phase overlap
   tests.
4. Testing each caster's light-space bounding volume against the mask
   hierarchy.
5. Compacting survivors into a dense draw buffer for indirect raster.

### 2.4 GPU Hierarchical Visibility

Reference: Hill and Collin, *Practical, Dynamic Visibility for Games*
(`https://blog.selfshadow.com/publications/practical-visibility/`)

GPU-driven visibility testing uses the hierarchical Z-buffer (HZB) and
indirect execution to eliminate CPU readback from the visibility pipeline.
Oxygen already applies this for its HZB construction pass and VSM-style
counted-indirect raster.

The principle for the shadow path: all analysis, culling, compaction, and
indirect argument generation happen on the GPU. The CPU publishes the draw
records and job metadata once per frame. The GPU does all per-cascade,
per-draw work.

## 3. Architecture Overview

### 3.1 Design Constraints

These are non-negotiable boundaries enforced throughout the design:

1. **Single authoritative draw stream.** The conventional shadow draw-record
   contract is the single caster stream. All components build on it; none
   replace it.
2. **No `O(cascades × scene)` CPU work.** ScenePrep produces one draw-record
   stream per view. Cascade-specific work is GPU-only.
3. **No CPU readback on the hot path.** The GPU pipeline from analysis through
   raster is fully self-contained.
4. **No receiver-object-sphere culling.** This approach was tried, invalidated,
   and rolled back (see §8). It must not be reintroduced.
5. **Reuse existing engine pipelines.** `DepthPrePass`, `ScreenHzbBuildPass`,
   bindless descriptor heap, counted-indirect execution, and VSM-style
   resource patterns are all reused rather than duplicated.

### 3.2 CPU Responsibilities

The CPU side is responsible for:

- **Scene preparation** via the existing `PreparedSceneFrame` model
- **Draw record construction** (`ConventionalShadowDrawRecordBuilder`)
  producing one `ConventionalShadowDrawRecord` per eligible shadow caster per
  view
- **Cascade geometry computation** in `ConventionalShadowBackend` — frustum
  slicing, bounding sphere fitting, texel snapping, light-view/projection
  matrices
- **Job metadata publication** — one `RasterShadowJob` and one
  `ConventionalShadowReceiverAnalysisJob` per cascade, uploaded to GPU-visible
  buffers
- **Pass scheduling** — ordering the GPU passes in the correct dependency
  chain

The CPU never filters draws per cascade, never reads back GPU analysis results,
and never builds receiver masks.

### 3.3 GPU Responsibilities

The GPU side is responsible for:

- **Receiver analysis** — deriving tight per-cascade light-space bounds from
  visible depth samples (SDSM core)
- **Receiver mask construction** — building tile-granularity occupancy masks
  per cascade
- **Caster culling and compaction** — testing draw records against receiver
  data and compacting survivors
- **Counted indirect raster** — executing only the compacted work

### 3.4 Execution Pipeline

The per-frame execution flow:

```text
DepthPrePass
    │
    ▼
ScreenHzbBuildPass              (produces main-view HZB)
    │
    ▼
ReceiverAnalysis                (produces tight per-cascade bounds from
    │                            visible depth samples via GPU atomics)
    ▼
ReceiverMask                    (five dispatches: clear → per-pixel tile
    │                            marking → dilation → hierarchy build
    │                            → finalize summary)
    ▼
CasterCulling                   (tests draw records against receiver
    │                            bounds + mask, compacts survivors)
    ▼
CountedIndirectRaster           (executes compacted work only)
```

No CPU readback occurs anywhere in this chain.

## 4. Data Structures

### 4.1 ConventionalShadowDrawRecord (32 bytes, GPU)

```cpp
struct ConventionalShadowDrawRecord {
  vec4     world_bounding_sphere;     // center.xyz, radius.w
  uint32   draw_index;                // index into per-draw metadata
  uint32   partition_index;           // mesh partition identity
  PassMask partition_pass_mask;       // which passes this partition
                                      //   participates in
  uint32   primitive_flags;           // static-caster, main-view-visible, etc.
};
```

Published once per frame per view by `ConventionalShadowDrawRecordBuilder`.
Reused by all cascades without duplication. This is the authoritative caster
identity stream.

### 4.2 ConventionalShadowReceiverAnalysisJob (144 bytes, CPU→GPU)

```cpp
struct ConventionalShadowReceiverAnalysisJob {
  mat4     light_rotation_matrix;          // pure light rotation (no translation)
  vec4     full_rect_center_half_extent;   // padded cascade sphere-fit reference
  vec4     legacy_rect_center_half_extent; // receiver-tightened ortho reference
  vec4     split_and_full_depth_range;     // .xy = cascade eye-depth split
                                           // .zw = light-space depth range
  vec4     shading_margins;                // .x = world units/texel
                                           // .y = constant bias
                                           // .z = normal bias
  uint32   target_array_slice;
  uint32   flags;
  uint32   _pad[2];
};
```

Published by `ConventionalShadowBackend` per cascade. Carries both the full
cascade-fit reference (for baseline comparison) and the legacy receiver-
tightened reference (the CPU best-effort). The GPU analysis compares its
sample-derived bounds against both.

### 4.3 ConventionalShadowReceiverAnalysis (96 bytes, GPU output)

```cpp
struct ConventionalShadowReceiverAnalysis {
  vec4     raw_xy_min_max;                 // tight light-space XY bounds from
                                           //   actual visible samples
  vec4     raw_depth_and_dilation;         // .xy = tight depth min/max
                                           // .zw = XY and depth dilation margins
  vec4     full_rect_center_half_extent;   // copy from job for downstream use
  vec4     legacy_rect_center_half_extent; // copy from job for downstream use
  vec4     full_depth_and_area_ratios;     // .x = full depth range start
                                           // .y = full depth range end
                                           // .z = sample_area / full_area
                                           // .w = sample_area / legacy_area
  float    full_depth_ratio;               // sample_depth / full_depth
  uint32   sample_count;                   // visible samples in this cascade
  uint32   target_array_slice;
  uint32   flags;                          // VALID, EMPTY, FALLBACK_TO_LEGACY
};
```

Produced entirely on GPU. The `full_depth_and_area_ratios` and
`full_depth_ratio` fields are the SDSM-style tightening metrics. A near
cascade with `full_area_ratio ≪ 1.0` has significant room for shadow-map
resolution improvement.

### 4.4 ConventionalShadowReceiverMaskSummary (96 bytes, GPU output)

```cpp
struct ConventionalShadowReceiverMaskSummary {
  vec4     full_rect_center_half_extent;   // copy from analysis for downstream
  vec4     raw_xy_min_max;                 // copy from analysis (tight bounds)
  vec4     raw_depth_and_dilation;         // copy from analysis (dilation margins)
  uint32   target_array_slice;
  uint32   flags;                          // VALID, EMPTY, HIERARCHY_BUILT
  uint32   sample_count;                   // visible samples
  uint32   occupied_tile_count;            // base-level occupied tiles after
                                           //   dilation
  uint32   hierarchy_occupied_tile_count;  // hierarchy-level occupied tiles
  uint32   base_tile_resolution;           // e.g. 128 for 4096/32
  uint32   hierarchy_tile_resolution;      // e.g. 32 for 128/4
  uint32   dilation_tile_radius;           // conservative tile dilation radius
  uint32   hierarchy_reduction;            // e.g. 4
  uint32   _pad0[3];
};
```

One per cascade. Carries forward receiver analysis data alongside mask
occupancy metrics.

### 4.4.1 Mask Buffers (GPU intermediates)

- **Raw mask buffer:** one `uint32` per tile per cascade. `CS_Analyze` writes
  `1` via `InterlockedOr`. Represents the undilated per-pixel tile hits.
- **Base mask buffer:** one `uint32` per tile per cascade. `CS_DilateMasks`
  reads the raw mask and writes dilated occupancy. This is the authoritative
  mask for downstream caster culling.
- **Hierarchy mask buffer:** one `uint32` per hierarchy tile per cascade.
  `CS_BuildHierarchy` OR-reduces the base mask. Used for fast broad-phase
  overlap rejection.
- **Count buffer:** two `uint32` per cascade — `[0]` = base occupied count,
  `[1]` = hierarchy occupied count.

Base resolution: `32 × 32` texel tiles for a `4096 × 4096` shadow map =
`128 × 128` tile grid. Hierarchy reduction factor `4` → `32 × 32` hierarchy
grid. Storage per cascade: `128 × 128 × 4 bytes = 64 KB` base + raw;
`32 × 32 × 4 bytes = 4 KB` hierarchy — moderate but acceptable for the
clear/dilate simplicity versus bit-packing.

### 4.5 ConventionalShadowCasterCullingPartition (32 bytes, CPU→GPU)

```cpp
struct ConventionalShadowCasterCullingPartition {
  uint32   record_begin;           // first draw record in this partition
  uint32   record_count;           // number of draw records
  uint32   command_uav_index;      // bindless UAV for indirect commands
  uint32   count_uav_index;        // bindless UAV for per-job counts
  uint32   max_commands_per_job;   // overflow guard (= record_count)
  uint32   partition_index;        // mesh partition identity
  PassMask pass_mask;              // which passes this partition serves
  uint32   _pad0;
};
```

One per contiguous draw-record partition in the authoritative draw-record
stream. Carries the output UAV indices so the shader can append surviving
commands independently per partition.

### 4.5.1 ConventionalShadowIndirectDrawCommand (20 bytes, GPU output)

```cpp
struct ConventionalShadowIndirectDrawCommand {
  uint32   draw_index;                   // root constant for bindless lookup
  uint32   vertex_count_per_instance;    // DrawInstanced arg 0
  uint32   instance_count;               // DrawInstanced arg 1
  uint32   start_vertex_location;        // DrawInstanced arg 2
  uint32   start_instance_location;      // DrawInstanced arg 3
};
```

Matches the `ExecuteIndirect(kDrawWithRootConstant)` layout: DWORD0 is the
per-draw root constant (`draw_index`), followed by the native draw arguments.
One command buffer per partition, sized `job_count × max_commands_per_job`.

### 4.5.2 Per-Job Count Buffers (GPU output)

- One `uint32` per cascade per partition — the number of surviving commands
  for that job.
- Zeroed via upload-copy before culling.
- Populated by atomic increment in the culling shader.
- Consumed as the count argument for `ExecuteIndirectCounted`.

## 5. Component Detail

### 5.1 Shadow Draw Record Construction

The draw-record builder (`ConventionalShadowDrawRecordBuilder`) iterates the
`PreparedSceneFrame` once and emits one `ConventionalShadowDrawRecord` per
eligible shadow caster. Eligibility is determined by the partition's pass mask
(must include a shadow raster pass).

Key properties:

- **One stream per view.** No cascade duplication.
- **Stable draw identity.** `draw_index` and `partition_index` are stable
  across frames for the same scene content, enabling frame-over-frame caching.
- **Self-contained caster geometry.** `world_bounding_sphere` is carried
  per-record for GPU-side spatial tests without requiring back-references into
  scene-prep.
- **Static/dynamic annotation.** `primitive_flags` carries
  `kStaticShadowCaster` and `kMainViewVisible` bits for potential
  static/dynamic splitting.

### 5.2 Receiver Analysis

Derives tight per-cascade light-space bounds from the actual distribution of
visible depth samples (SDSM core). Implemented as three GPU compute dispatches:

| Dispatch | Thread group | Grid | Purpose |
| - | - | - | - |
| `CS_Clear` | 64×1×1 | `ceil(jobs/64)` | Initialize raw atomic accumulators |
| `CS_Analyze` | 8×8×1 | `ceil(W/8) × ceil(H/8)` | Per-pixel: reconstruct world pos, classify into cascade, project into light space, accumulate via atomics |
| `CS_Finalize` | 64×1×1 | `ceil(jobs/64)` | Decode atomics, compute area/depth ratios, write final analysis |

#### CS_Clear

Initializes a `ConventionalShadowReceiverAnalysisRaw` record per job:

- `min_{x,y,z}_ordered ← FloatToOrdered(+FLT_MAX)` (any real sample will be smaller)
- `max_{x,y,z}_ordered ← FloatToOrdered(-FLT_MAX)` (any real sample will be larger)
- `sample_count ← 0`

#### CS_Analyze

For each HZB mip-0 pixel (approximately half the full-resolution depth buffer):

1. **Load depth.** Skip sky pixels (`depth ≤ 0` in reversed-Z, where
   near = 1.0, far = 0.0).
2. **Reconstruct world position.** Pixel → UV → NDC → clip → world via the
   inverse view-projection matrix.
3. **Compute eye depth.** `max(0, -(view_matrix × world_pos).z)` — the
   negated view-space Z gives the positive distance from the camera.
4. **Classify into cascade.** Compare eye depth against each job's
   `split_begin` / `split_end`. First cascade uses `≥` at the near boundary
   (inclusive); subsequent cascades use `>` (exclusive boundary). This prevents
   double-counting at split boundaries.
5. **Project into light space.** `light_rotation_matrix × world_pos` — the
   same pure rotation used by the CPU to define the cascade geometry.
6. **Accumulate atomically.** `InterlockedMin` / `InterlockedMax` on the raw
   buffer using `FloatToOrdered()` encoding.

The float-to-ordered-uint encoding is the standard technique for atomic
min/max on IEEE 754 floats: positive → flip sign bit; negative → flip all
bits. This produces a monotonically ordered uint representation suitable for
integer atomics.

#### CS_Finalize

Per job:

1. Decode ordered-uint atomics back to floats via `OrderedToFloat()`.
2. Compute conservative dilation margins from `world_units_per_texel`,
   `constant_bias`, and `normal_bias`.
3. Compute the SDSM tightening ratios:
   - `full_area_ratio = sample_area / full_cascade_area`
   - `legacy_area_ratio = sample_area / legacy_rect_area`
   - `full_depth_ratio = sample_depth_span / full_depth_span`
4. Write the final `ConventionalShadowReceiverAnalysis` record.
5. Set `VALID` flag if `sample_count > 0`, otherwise `EMPTY`.

The raw bounds are deliberately **not dilated** in this pass — they record
the precise visible-sample footprint. Dilation margins are stored alongside
for downstream components (receiver mask, caster culling) to apply
contextually.

#### GPU synchronization

- Clear → Analyze: the raw buffer stays in `kUnorderedAccess` for both
  dispatches. The resource state tracker's default auto-memory-barrier mode
  inserts a UAV barrier between them.
- Analyze → Finalize: the raw buffer transitions from `kUnorderedAccess` to
  `kGenericRead`, which is a proper state transition barrier. The analysis
  buffer transitions to `kUnorderedAccess` for the Finalize write.

#### Input source: HZB mip 0 vs full-res depth

The pass reads from the HZB "closest" texture (mip 0), which is the max-
reduced (reversed-Z: max = closest to camera) half-resolution depth product.
This is standard SDSM practice — the reduced resolution is sufficient for
bounds analysis and significantly reduces atomic contention versus full-
resolution depth.

#### Scaling characteristics

The pass is `O(screen_pixels × cascades)` — every HZB pixel iterates over all
jobs. With 4 cascades this is a 4× cost over a single-cascade pass. This is
acceptable for typical cascade counts (2–6). If cascade count exceeds ~8, a
tile-classification pre-pass would amortize the per-pixel work, but that is
outside current scope.

### 5.3 Light-Space Receiver Mask

Converts the tight receiver bounds from the receiver analysis into a sparse
tile-granularity occupancy mask that enables fine-grained caster culling.

**Why the tight rect alone is not sufficient:**

The receiver analysis produces a tight axis-aligned bounding rect in light
space, but it treats the entire rect as uniformly occupied. In practice,
visible receivers cluster in specific regions of the shadow map — e.g., ground
plane, nearby walls — leaving large regions of the tight rect unoccupied. A
tile mask captures this sparsity.

Five GPU compute dispatches:

| Dispatch | Thread group | Grid | Purpose |
| - | - | - | - |
| `CS_ClearMasks` | 64×1×1 | `ceil(max_entries/64)` | Zero raw, base, hierarchy masks and count buffer |
| `CS_Analyze` | 8×8×1 | `ceil(W/8) × ceil(H/8)` | Per-pixel: classify into cascade, project to light space, mark raw tile via `InterlockedOr` |
| `CS_DilateMasks` | 64×1×1 | `ceil(jobs×base_tiles/64)` | Per-tile: read raw mask, apply conservative dilation radius, write base mask, count occupied tiles |
| `CS_BuildHierarchy` | 64×1×1 | `ceil(jobs×hier_tiles/64)` | Per-hierarchy-tile: OR-reduce base mask tiles, count hierarchy occupied tiles |
| `CS_Finalize` | 64×1×1 | `ceil(jobs/64)` | Per-job: write `ConventionalShadowReceiverMaskSummary` from analysis + tile counts |

#### CS_ClearMasks

Zeros all entries in raw mask, base mask, hierarchy mask, and count buffers.
The clear dispatch grid covers the maximum of all buffer sizes.

#### CS_Analyze

For each HZB mip-0 pixel:

1. **Load depth.** Skip sky pixels (`depth ≤ 0` in reversed-Z).
2. **Reconstruct world position.** Pixel → UV → NDC → clip → world via the
   inverse view-projection matrix.
3. **Compute eye depth.** `max(0, -(view_matrix × world_pos).z)`.
4. **Classify into cascade.** Same split logic as receiver analysis — first
   cascade uses `≥` at near boundary, subsequent use `>`.
5. **Project into light space.** `light_rotation_matrix × world_pos`.
6. **Quantize to tile coordinate.** Map the light-space XY position into the
   tile grid using `full_rect_center_half_extent` as the mapping domain.
7. **Mark tile.** `InterlockedOr(raw_mask[tile_index], 1)`.

**Mapping domain choice:** The tile grid is defined over the
`full_rect_center_half_extent` (the full cascade sphere-fit rectangle), not
the tight bounds from receiver analysis. This means the grid covers the entire
cascade orthographic projection, and receiver occupancy within that grid
reveals the true spatial sparsity. Using the full rect also ensures the mask
coordinate system is stable and directly usable by caster culling's bounding
sphere tests without coordinate remapping.

The per-pixel work mirrors receiver analysis's `CS_Analyze` (world
reconstruction, cascade classification, light-space projection). The write is
a single `InterlockedOr` setting a tile to occupied — far cheaper than
receiver analysis's six ordered-float atomic min/max operations.

#### CS_DilateMasks

Per base-mask tile:

1. Read the dilation radius from the analysis record's `xy_dilation_margin`
   converted to tile units via `ComputeDilationTileRadius()`.
2. For the current tile, scan the raw mask within a `±radius` neighborhood.
3. If any raw-mask tile within the neighborhood is occupied, write `1` to the
   base mask.
4. Atomically increment the per-job occupied-tile count.

By separating dilation into its own pass reading from the raw mask (SRV) and
writing the base mask (UAV), the dilation neighborhood reads are coherent and
free of write-after-write hazards.

#### CS_BuildHierarchy

Per hierarchy tile:

1. Compute the base-tile origin for this hierarchy tile using the reduction
   factor (default 4).
2. Scan all base-mask tiles covered by this hierarchy tile.
3. If any base-mask tile is occupied, write `1` to the hierarchy mask.
4. Atomically increment the per-job hierarchy occupied-tile count.

The hierarchy provides fast broad-phase overlap rejection for caster culling.
A caster whose light-space projection does not overlap any occupied hierarchy
tile can be rejected without checking the full-resolution base mask.

#### CS_Finalize

Per job:

1. Read the receiver analysis record and the tile counts from the count buffer.
2. Assemble a `ConventionalShadowReceiverMaskSummary` carrying forward the
   analysis data alongside mask occupancy metrics.
3. Set `VALID` + `HIERARCHY_BUILT` flags if the analysis was valid and had
   samples; otherwise set `EMPTY`.

#### GPU synchronization

- Job upload: `CopySource` / `CopyDest` barriers for the upload → device copy.
- ClearMasks: raw_mask, base_mask, hierarchy_mask, count_buffer all in
  `kUnorderedAccess`.
- Analyze: depth texture → `kShaderResource`, job_buffer → `kGenericRead`,
  raw_mask → `kUnorderedAccess`.
- DilateMasks: raw_mask → `kGenericRead`, base_mask → `kUnorderedAccess`,
  count_buffer → `kUnorderedAccess`, analysis_buffer → `kShaderResource`.
- BuildHierarchy: base_mask → `kGenericRead`, hierarchy_mask →
  `kUnorderedAccess`, count_buffer → `kUnorderedAccess`.
- Finalize: count_buffer → `kGenericRead`, summary_buffer →
  `kUnorderedAccess`.
- Final states: base_mask, hierarchy_mask, summary → `kShaderResource`;
  raw_mask, count_buffer → `kCommon`.

#### Storage trade-off

The implementation uses one `uint32` per tile rather than bit-packing.
At `128 × 128` tiles × 4 bytes × 4 cascades = `256 KB` for raw + `256 KB`
for base + `16 KB` for hierarchy — modest GPU memory. The advantage is
simpler atomic writes (`InterlockedOr` on a uint vs. bit-offset atomics) and
cleaner dilation reads. Bit-packing can be revisited if memory pressure
requires it.

#### Scaling characteristics

The per-pixel `CS_Analyze` dispatch is the dominant cost — same complexity as
receiver analysis's per-pixel pass. The remaining four dispatches (clear,
dilate, hierarchy, finalize) are tile-count-proportional and negligible.

#### Constants structure (208 bytes)

```cpp
struct ConventionalShadowReceiverMaskPassConstants {
  uint32   depth_texture_index;
  uint32   job_buffer_index;
  uint32   analysis_buffer_index;
  uint32   raw_mask_uav_index;
  uint32   raw_mask_srv_index;
  uint32   base_mask_uav_index;
  uint32   base_mask_srv_index;
  uint32   hierarchy_mask_uav_index;
  uint32   hierarchy_mask_srv_index;
  uint32   count_buffer_uav_index;
  uint32   count_buffer_srv_index;
  uint32   summary_buffer_uav_index;
  uvec2    screen_dimensions;
  uint32   job_count;
  uint32   base_tile_resolution;
  uint32   hierarchy_tile_resolution;
  uint32   base_tiles_per_job;
  uint32   hierarchy_tiles_per_job;
  uint32   hierarchy_reduction;
  mat4     inverse_view_projection;        // offset 80
  mat4     view_matrix;                    // offset 144
};
```

### 5.4 GPU Caster Culling and Compaction

Uses the receiver analysis and mask to discard casters that cannot produce
visible shadows, then compacts the survivors into a dense draw buffer ready
for counted-indirect raster.

One GPU compute dispatch per partition per frame:

| Dispatch | Thread group | Grid | Purpose |
| - | - | - | - |
| `CS` | 64×1×1 | `ceil(record_count/64) × job_count` | Per-(draw, cascade): cull against receiver data, append surviving commands |

The partition index is passed as a root constant. Each dispatch covers all
draw records within one contiguous partition against all cascade jobs.

#### Culling pipeline per thread

1. **Early-out on degenerate draws.** Skip records with zero vertex/instance
   count or zero-radius bounding sphere.
2. **Skip invalid cascades.** If the mask summary for this job is not `VALID`
   or has no samples, the thread exits — no shadow work is needed for an
   empty cascade.
3. **Broad phase A — dilated tight rect (XY).** Project the caster's world
   bounding sphere into light space via the job's `light_rotation_matrix`.
   Test sphere center ± radius against the tight bounds (`raw_xy_min_max`)
   dilated by the XY dilation margin. Reject if fully outside.
4. **Broad phase B — one-sided receiver depth gate (Z).** Treat the tight
   depth interval as a receiver lower bound, not as a symmetric overlap slab.
   In Oxygen's directional-light view, larger light-space `z` is closer to the
   light. A caster can still project onto the receiver set while sitting
   anywhere in front of the receiver interval, so it must not be rejected
   merely because it extends past the receiver maximum depth. Reject only when
   the caster sphere's maximum light-space `z` is still behind the dilated
   receiver minimum depth. This preserves legitimate occluders in dense scenes
   such as Sponza, where nearby architectural details often sit in front of
   the visible receiver interval while still casting onto it.
5. **Medium phase — hierarchy mask overlap.** Compute the sphere's tile-grid
   footprint using `ComputeSphereTileBounds()` against the
   `full_rect_center_half_extent` mapping domain (same coordinate system as
   the receiver mask). Divide by the hierarchy reduction factor and scan the
   hierarchy mask. Reject if no hierarchy tile overlaps.
6. **Fine phase — base mask overlap.** Scan the base mask tiles within the
   sphere's tile footprint. Reject if no base tile overlaps.
7. **Emit.** Atomically increment the per-job command count and append a
   `ConventionalShadowIndirectDrawCommand` into the partition's command
   buffer at `job_index × max_commands_per_job + slot`. Overflow is
   guarded by `max_commands_per_job`.

#### Partition model

The CPU collects contiguous draw-record runs grouped by `partition_index` from
the authoritative draw-record stream. Each partition gets its own command
buffer and count buffer. This preserves the existing raster-pass partition
structure so counted-indirect raster can execute one `ExecuteIndirectCounted`
call per (partition, cascade) pair without restructuring the raster pass.

#### Coordinate system alignment

The tile-bounds computation uses `full_rect_center_half_extent` — the same
mapping domain used by the receiver mask's `CS_Analyze` when building the
mask. This ensures the caster's tile footprint is directly comparable to the
mask without any coordinate remapping.

The same light-space convention also governs the depth gate: with the
directional-light `lookAtRH` basis used by Oxygen, moving toward the light
increases light-space `z`. That means the culling test is intentionally
one-sided in depth: "fully behind the nearest receiver depth" is rejectable;
"in front of the receiver interval" is still eligible and must continue to
mask overlap testing.

#### Count buffer zeroing

Per-job counts are zeroed via upload-copy from a pre-zeroed upload buffer
rather than a separate GPU clear dispatch. This avoids an additional dispatch
and barrier round-trip.

#### GPU synchronization

- Upload copies: job, partition, and count-clear buffers transition through
  `kCopySource` / `kCopyDest`.
- Before dispatch: job + partition → `kGenericRead`; mask summary, base mask,
  hierarchy mask → `kShaderResource`; command + count buffers →
  `kUnorderedAccess`.
- After dispatch: command + count buffers → `kIndirectArgument`
  (ready for raster consumption); job + partition → `kCommon`.
- No inter-partition barriers are needed — each partition writes to
  independent command/count buffers.

#### Constants structure (64 bytes)

```cpp
struct ConventionalShadowCasterCullingPassConstants {
  uint32   draw_record_buffer_index;
  uint32   draw_metadata_index;
  uint32   job_buffer_index;
  uint32   receiver_mask_summary_index;
  uint32   receiver_mask_base_index;
  uint32   receiver_mask_hierarchy_index;
  uint32   partition_buffer_index;
  uint32   job_count;
  uint32   base_tile_resolution;
  uint32   hierarchy_tile_resolution;
  uint32   base_tiles_per_job;
  uint32   hierarchy_tiles_per_job;
  uint32   hierarchy_reduction;
  uint32   partition_count;
  uint32   _pad[2];
};
```

#### Scaling characteristics

At `~400 records × 4 cascades = ~1600 threads` per partition, the pass is
extremely lightweight. The three-level culling chain (rect → hierarchy →
base) keeps per-thread work short. If draw counts grow to thousands, the
thread model remains efficient — one thread per (draw, cascade) with
independent output slots.

#### Optional companion bounds

If draw-record world bounding spheres prove too coarse for mask overlap (i.e.,
many false survivors pass the mask test), an additional cull-bounds buffer
can provide tighter light-space AABBs per draw record. This extends the
draw-record contract without invalidating it.

### 5.5 Counted-Indirect Raster

Executes only the compacted caster work produced by the culling pass.

**Pipeline ordering:**

The shadow raster pass executes after the complete
`ScreenHzb → ReceiverAnalysis → ReceiverMask → CasterCulling` chain,
while still completing before the main shaded lighting path consumes the
conventional shadow texture.

**Execution model:**

For each cascade job:

1. Set render target to the cascade's shadow-map array slice.
2. Clear depth.
3. Bind per-job shadow view constants.
4. For each partition that participates in shadow casting:
   - Select and bind the appropriate PSO (opaque or masked), with
     change-tracking to avoid redundant PSO binds.
   - Issue one `ExecuteIndirectCounted` call consuming:
     - Command buffer at offset
       `job_index × max_commands_per_job × sizeof(IndirectDrawCommand)`
     - Count buffer at offset `job_index × sizeof(uint32)`
     - Maximum command count = `max_commands_per_job`
     - Layout = `kDrawWithRootConstant` (root constant carries `draw_index`
       for bindless lookup)

**Resource state contract:**

The culling pass leaves command and count buffers in `kIndirectArgument`. The
raster pass consumes them in `kIndirectArgument` and leaves them in that state.

## 6. Key Implementation Details

### 6.1 Reversed-Z Depth Convention

Oxygen uses reversed-Z depth throughout:

- Near plane → depth `1.0`
- Far plane / sky → depth `0.0`
- The HZB "closest" texture uses `max()` reduction (closest to camera = largest
  depth value)
- The HZB "furthest" texture uses `min()` reduction

The receiver analysis shader skips pixels where `depth ≤ 0.0` — this correctly
rejects sky and far-plane samples.

### 6.2 Light-Space Coordinate System

The `light_rotation_matrix` is a pure rotation (no translation) that
transforms world-space positions into a space aligned with the light direction.
Both the CPU cascade geometry computation and the GPU analysis shader use the
same matrix, ensuring the GPU-derived bounds are directly comparable to the
CPU-published `full_rect_center_half_extent` and
`legacy_rect_center_half_extent`.

The light-space Z axis is aligned with the light direction. XY define the
shadow map plane. The orthographic projection for raster is derived from a
separate translated light-view matrix (`lookAt` from a pulled-back eye
position), but the receiver analysis works in the rotation-only space to avoid
any light-eye position dependency.

### 6.3 Cascade Split Classification

Eye depth is derived by projecting the reconstructed world position through the
view matrix: `eye_depth = max(0, -(V × world_pos).z)`.

Split boundaries come from the CPU's frustum partition. The first cascade uses
an inclusive near boundary (`eye_depth >= split_begin`), and all subsequent
cascades use an exclusive near boundary (`eye_depth > split_begin`). This
ensures each visible sample contributes to exactly one cascade.

### 6.4 Float-To-Ordered-Uint Atomic Encoding

GPU atomics only support integer min/max. To perform atomic min/max on
IEEE 754 floats:

```text
FloatToOrdered(f):
    bits = asuint(f)
    if sign bit set: return ~bits        (flip all → monotonic negative range)
    else:            return bits ^ 0x80000000  (flip sign → above negative range)

OrderedToFloat(o):
    if bit 31 set: return asfloat(o ^ 0x80000000)  (was positive: undo sign flip)
    else:          return asfloat(~o)               (was negative: undo full flip)
```

This maps the IEEE 754 total order to an unsigned integer total order,
enabling `InterlockedMin` / `InterlockedMax` to compute float min/max.

### 6.5 Texel Snapping and Stability

The CPU snaps each cascade's light-space center to texel boundaries before
computing the final orthographic projection:

```cpp
snapped.x = round(center.x / texel_size) * texel_size
snapped.y = round(center.y / texel_size) * texel_size
```

This prevents sub-texel shadow-map jitter as the camera moves. The snapped
center defines the `legacy_rect_center_half_extent` published to the GPU,
while the unsnapped sphere-fit center defines the `full_rect_center_half_extent`.

### 6.6 Dilation Margins

The receiver analysis finalize pass computes conservative dilation margins:

```cpp
xy_margin = max(world_units_per_texel,
                constant_bias + normal_bias + 2 * world_units_per_texel)
depth_margin = max(world_units_per_texel,
                   constant_bias + normal_bias)
```

These margins are stored in the analysis record but **not applied** to the raw
bounds. The receiver mask (dilation pass) and caster culling apply them
contextually. This separation keeps the raw analysis truthful and the dilation
policy tunable.

## 7. Cascade Tuning Architecture

This section describes the tuning surface for cascade placement, transition
blending, and shadow-range control.

### 7.1 Quality Tier Budget

Classic CSM quality-tier budget:

| Oxygen quality tier | Maximum classic CSM resolution |
| - | - |
| `Low` | `1024` |
| `Medium` ("Normal") | `2048` |
| `High` | `3072` for one dominant directional light, else `2048` |
| `Ultra` | `4096` for one dominant directional light, else `3072` |

Authored shadow-resolution hints map to:

| Authored hint | Requested classic CSM resolution |
| - | - |
| `Low` | `1024` |
| `Medium` | `2048` |
| `High` | `3072` |
| `Ultra` | `4096` |

The backend resolves the final classic CSM resolution by taking the authored
light `resolution_hint` and clamping it to the active `ShadowQualityTier`
budget.

### 7.2 Tuning Contract

| Tuning concern | Oxygen contract |
| - | - |
| authored max near-CSM distance | `max_shadow_distance` in `CascadedShadowSettings` |
| runtime scalar on the authored max distance | `rndr.shadow.csm.distance_scale` |
| authored cascade count | existing `cascade_count`, clamped by `rndr.shadow.csm.max_cascades` |
| geometric-series bias toward the camera | existing `distribution_exponent`, active in generated-split mode |
| authored overlap fraction between neighboring cascades | `transition_fraction` |
| runtime scalar on transition overlap | `rndr.shadow.csm.transition_scale` |
| fade-out region at the end of CSM coverage | `distance_fadeout_fraction` |
| global cap for classic CSM resolution | optional `rndr.shadow.csm.max_resolution` |

### 7.3 Authored Model

`CascadedShadowSettings` supports both generated and manual split policies:

```cpp
enum class DirectionalCsmSplitMode : std::uint8_t {
  kGenerated,
  kManualDistances,
};

struct CascadedShadowSettings {
  std::uint32_t cascade_count = 4;
  DirectionalCsmSplitMode split_mode = DirectionalCsmSplitMode::kGenerated;
  float max_shadow_distance = 160.0F;
  std::array<float, 4> cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F };
  float distribution_exponent = 3.0F;
  float transition_fraction = 0.1F;
  float distance_fadeout_fraction = 0.1F;
};
```

Design rules:

- `kGenerated` is the recommended default for newly authored directional
  lights.
- `kManualDistances` exists for legacy compatibility and explicit technical art
  overrides.
- Legacy imported content that only carries `cascade_distances` maps to
  `kManualDistances`.

### 7.4 Runtime CVars

- `rndr.shadow.csm.distance_scale` — default `1.0`, clamp `[0.0, 2.0]`,
  multiplies authored `max_shadow_distance`
- `rndr.shadow.csm.transition_scale` — default `1.0`, clamp `[0.0, 2.0]`,
  multiplies authored `transition_fraction`
- `rndr.shadow.csm.max_cascades` — default `4`, clamp `[1, 4]`, hard runtime
  cap for active conventional cascades
- `rndr.shadow.csm.max_resolution` — optional, default `0` (disabled), final
  hard ceiling for classic CSM raster resolution

### 7.5 Split Formula

Generated mode uses `distribution_exponent` as the primary split policy:

```text
effective_max_distance =
  authored.max_shadow_distance * clamp(rndr.shadow.csm.distance_scale, 0, 2)

effective_cascade_count =
  min(authored.cascade_count, rndr.shadow.csm.max_cascades)

split_end(i) =
  near_plane + accumulated_geometric_fraction(
    authored.distribution_exponent, i + 1, effective_cascade_count)
  * (effective_max_distance - near_plane)
```

Where `accumulated_geometric_fraction` is a geometric-series accumulator:

- weights: `1, exponent, exponent², ...`
- split `i` ends at the accumulated weight fraction through cascade `i`

Larger exponent pushes more detail toward the camera; smaller exponent
distributes more evenly.

### 7.6 Transition and Fade Policies

**Cascade-to-cascade transition:**

```text
effective_transition_fraction =
  authored.transition_fraction
  * clamp(rndr.shadow.csm.transition_scale, 0, 2)
```

Per-cascade transition widths are computed on the CPU and published explicitly
to the shader.

**End-of-range fade:**

```text
fade_begin =
  effective_max_distance
  - effective_max_distance * authored.distance_fadeout_fraction
```

Transition fraction blends *between cascades*; distance fadeout blends *from
the last cascade to unshadowed*. These are independent concerns.

### 7.7 C++ Ownership

- `src/Oxygen/Scene/Light/LightCommon.h` — authored `CascadedShadowSettings`
- `src/Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeLightBindings.cpp` —
  scripting/editor exposure
- `src/Oxygen/Renderer/Types/DirectionalShadowCandidate.h` — renderer-space
  tuning fields
- `src/Oxygen/Renderer/LightManager.cpp` — candidate publication
- `src/Oxygen/Renderer/Internal/ConventionalShadowBackend.cpp` — generated
  splits, cascade clamping, transition/fade data

### 7.8 HLSL Ownership

- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/ShadowHelpers.hlsli` —
  cascade selection, transition blending, last-cascade fadeout
- Directional shadow metadata HLSL layout — per-cascade transition/fade
  parameters

## 8. Design Decisions

### 8.1 Why Not CPU-Side Receiver Object Bounds?

An earlier approach used CPU-side whole-object bounding spheres from visible
receivers to derive per-cascade culling volumes. It was implemented, validated
against the canonical benchmark, and rejected because:

1. **Receiver truth was wrong.** Whole visible receiver objects are not the
   same as the visible receiver footprint.
2. **Bounds were too coarse.** Large architectural pieces (roofs, walls)
   expanded the receiver volume to approximately the full scene.
3. **Predicate was too weak.** Sphere-vs-rect-slab is too conservative when
   both sides are whole-object bounds.

This resulted in a compaction ratio of 1.000 (zero culling) on the canonical
benchmark. The GPU-driven SDSM-based receiver analysis (§5.2) replaced it.

### 8.2 Why Per-Pixel Analysis Instead of Object Bounds?

Per-pixel analysis directly observes the actual visible receiver footprint in
screen space. It naturally produces tight bounds proportional to screen
coverage rather than world-space object extents. This is the core SDSM insight
(§2.2) and is fundamental to achieving non-trivial caster culling on dense
architectural scenes.

### 8.3 Why a Two-Level Mask Hierarchy?

A single flat tile mask requires scanning all tiles within a caster's
footprint. The hierarchy (base + OR-reduced coarse level) provides fast
broad-phase rejection: a caster that misses all coarse tiles is rejected
without touching the fine mask. This keeps per-thread culling work short even
when the tile grid is large (128 × 128).

### 8.4 Why One-Sided Depth Culling?

Symmetric depth-slab culling (reject if caster is outside the receiver depth
range on either side) is incorrect for shadow casting. A caster in front of
all receivers can still cast shadows onto them. Only casters fully *behind*
(farther from the light than) the deepest receiver are guaranteed to produce
no visible shadow. The one-sided test preserves all legitimate occluders.

## 9. Future Extensions

### 9.1 Static / Dynamic Update Budgeting

If the counted-indirect raster still exceeds the shadow budget, static and
dynamic casters can be split using the `kStaticShadowCaster` flag already
present in draw records. Static shadow-map regions would be cached and
invalidated only when the light direction or camera moves significantly.

### 9.2 No Additional Shadow Specialization Surface

`CSM-7` does not add a new public shadow-specialization authoring surface.
The current conventional path already derives the important shadow behavior
from existing ownership points:

- scene/renderable shadow participation from the existing node-level
  `casts_shadows` gate
- alpha-tested shadow behavior from material domain plus alpha-test flags
- two-sided shadow rasterization from the existing material double-sided flag

This keeps the conventional path on one authoritative draw stream and avoids
introducing redundant shadow-only knobs, shadow-only materials, or shadow-only
LOD policy matrices without evidence that they are needed.

### 9.3 Tighter Cull Bounds

If world bounding spheres prove too coarse for mask overlap testing, an
additional cull-bounds buffer can provide tighter light-space AABBs per draw
record. This extends the draw-record contract without invalidating the
existing culling pipeline.

## 10. CSM-7 Final Baseline

Effective `2026-04-02`, the authoritative conventional-shadow baseline package
for future work is:

- capture stem:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline`
- RenderDoc capture:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline_frame350.rdc`
- benchmark log:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.benchmark.log`

Future conventional-shadow changes should compare against this package unless a
later package explicitly replaces it.

### 10.1 Locked Analysis Reports

- shadow timing:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.shadow_timing.txt`
- shader timing:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.shader_timing.txt`
- screen HZB timing:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.screen_hzb_timing.txt`
- receiver-analysis timing:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.receiver_analysis_timing.txt`
- receiver-mask timing:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.receiver_mask_timing.txt`
- caster-culling timing:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.caster_culling_timing.txt`
- receiver-analysis proof:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.receiver_analysis_report.txt`
- receiver-mask proof:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.receiver_mask_report.txt`
- caster-culling proof:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.caster_culling_report.txt`
- counted-indirect raster proof:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.raster_indirect_report.txt`
- normalized baseline comparison versus locked `CSM-2`:
  `out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline.baseline_compare.txt`

### 10.2 Structural Snapshot

- job count: `4`
- valid receiver-analysis jobs: `2`
- sampled receiver-analysis jobs: `2`
- receiver-mask dispatches: `5`
- receiver-mask occupied base tiles: `655 / 16384` (`0.039978`)
- receiver-mask occupied hierarchy tiles: `56 / 1024` (`0.054688`)
- input shadow draw records before culling: `401`
- emitted shadow draws after culling: `165`
- rejected draw replays versus full 4-cascade replay: `1439 / 1604`
  (`0.897132`)
- raster work events: `169`
- raster draw work events: `165`
- all raster draw work events are counted-indirect: `true`

### 10.3 Timing Snapshot

- `ConventionalShadowRasterPass`: `1.885536 ms`
- `ShaderPass`: `8.732192 ms`
- `ScreenHzbBuildPass`: `0.428096 ms`
- `ConventionalShadowReceiverAnalysisPass`: `0.558336 ms`
- `ConventionalShadowReceiverMaskPass`: `0.590080 ms`
- `ConventionalShadowCasterCullingPass`: `0.666176 ms`
- total measured shadow path (`HZB + receiver analysis + receiver mask + caster culling + raster`):
  `4.128224 ms`

### 10.4 Delta Versus Locked CSM-2

- shadow raster GPU time: `22.460064 ms -> 1.885536 ms`
  (`-91.604939%`)
- total measured shadow path: `23.875776 ms -> 4.128224 ms`
  (`-82.709571%`)
- shadow draw work events: `1604 -> 165`
- shadow draw work ratio versus baseline: `0.102868`
- shader pass GPU time: `9.193984 ms -> 8.732192 ms`
  (`-5.022763%`)
- directional summary match: `true`
- prepared shadow bounds match: `true`

### 10.5 Reproduction

Capture:

```powershell
.\tools\csm\Run-ConventionalShadowBaseline.ps1 `
  -Frame 350 `
  -RunFrames 420 `
  -Fps 50 `
  -Output out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline
```

Full analysis:

```powershell
.\tools\csm\Analyze-ConventionalShadowCsm5.ps1 `
  -CapturePath out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline_frame350.rdc `
  -OutputStem out/build-ninja/analysis/csm/csm7_baseline/release_frame350_csm7_baseline
```
