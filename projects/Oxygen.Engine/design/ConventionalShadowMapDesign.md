# Conventional Cascaded Shadow Map Architecture — Oxygen Engine

Status: living document
Audience: renderer engineers working on Oxygen's conventional directional
shadow path

This document captures the complete design of Oxygen's conventional directional
cascaded shadow map (CSM) pipeline, including the theoretical foundations, the
Oxygen-specific architecture, each implementation phase, and their
interdependencies.

---

## 1. Problem Statement

Oxygen needs a production-grade conventional cascaded shadow map path that
serves as:

- the primary shipping shadow solution for directional lights
- a reliable fallback when the virtual shadow map (VSM) path is disabled or
  unavailable
- a well-bounded GPU workload that does not scale with `O(cascades × scene)`
  on the CPU

The previous approach — culling casters against CPU-derived receiver object
bounding spheres — was invalidated because whole-object spheres are too coarse
to produce any meaningful culling on real-world architectural scenes. The new
architecture replaces that approach entirely.

## 2. Theoretical Foundations

The design draws on three published techniques.

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

This replaces the invalidated approach of using CPU-side receiver object
bounding spheres, which were too coarse and produced zero useful culling on
real scenes.

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

## 3. Oxygen Architecture

### 3.1 Design Constraints

These are non-negotiable boundaries enforced throughout the design:

1. **`CSM-1` stays.** The conventional shadow draw-record contract is the
   single authoritative caster stream. All phases build on it; none replace it.
2. **No `O(cascades × scene)` CPU work.** ScenePrep produces one draw-record
   stream per view. Cascade-specific work is GPU-only.
3. **No CPU readback on the hot path.** The GPU pipeline from analysis through
   raster is fully self-contained.
4. **No receiver-object-sphere culling.** This approach was tried, invalidated,
   and rolled back. It must not be reintroduced.
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

The complete target execution flow per frame:

```text
DepthPrePass
    │
    ▼
ScreenHzbBuildPass          (produces main-view HZB)
    │
    ▼
CSM-2: ReceiverAnalysis     (produces tight per-cascade bounds from visible
    │                        depth samples via GPU atomics)
    ▼
CSM-3: ReceiverMask         (five dispatches: clear → per-pixel tile marking
    │                        → dilation → hierarchy build → finalize summary)
    ▼
CSM-4: CasterCulling        (tests draw records against receiver bounds +
    │                        mask, compacts survivors)
    ▼
CSM-5: CountedIndirectRaster (executes compacted work only)
```

No CPU readback occurs anywhere in this chain.

## 4. Data Products

### 4.1 CSM-1: ConventionalShadowDrawRecord (32 bytes, GPU)

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

### 4.2 CSM-2: ConventionalShadowReceiverAnalysisJob (144 bytes, CPU→GPU)

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
tightened reference (the current CPU best-effort). The GPU analysis can
compare its sample-derived bounds against both.

### 4.3 CSM-2: ConventionalShadowReceiverAnalysis (96 bytes, GPU output)

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

### 4.4 CSM-3: ConventionalShadowReceiverMaskSummary (96 bytes, GPU output)

```cpp
struct ConventionalShadowReceiverMaskSummary {
  vec4     full_rect_center_half_extent;   // copy from analysis for downstream
  vec4     raw_xy_min_max;                 // copy from analysis (tight bounds)
  vec4     raw_depth_and_dilation;         // copy from analysis (dilation margins)
  uint32   target_array_slice;
  uint32   flags;                          // VALID, EMPTY, HIERARCHY_BUILT
  uint32   sample_count;                   // visible samples (from CSM-2)
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

Published one per cascade. Carries forward CSM-2 analysis data alongside the
mask occupancy metrics.

### 4.4.1 CSM-3: Mask Buffers (GPU intermediates)

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
clear/dilate simplicity versus bit-packing

### 4.5 CSM-4: ConventionalShadowCasterCullingPartition (32 bytes, CPU→GPU)

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

One per contiguous draw-record partition in the authoritative `CSM-1`
stream. Carries the output UAV indices so the shader can append surviving
commands independently per partition.

### 4.5.1 CSM-4: ConventionalShadowIndirectDrawCommand (20 bytes, GPU output)

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

### 4.5.2 CSM-4: Per-Job Count Buffers (GPU output)

- One `uint32` per cascade per partition — the number of surviving commands
  for that job.
- Zeroed via upload-copy before culling.
- Populated by atomic increment in the culling shader.
- Consumed by CSM-5 as the count argument for `ExecuteIndirectCounted`.

## 5. Phase Detail

### 5.1 CSM-1 — Shadow Draw Record Contract

**Status:** complete

The draw-record builder iterates the `PreparedSceneFrame` once and emits one
`ConventionalShadowDrawRecord` per eligible shadow caster. Eligibility is
determined by the partition's pass mask (must include a shadow raster pass).

Key properties:

- **One stream per view.** No cascade duplication.
- **Stable draw identity.** `draw_index` and `partition_index` are stable
  across frames for the same scene content, enabling frame-over-frame caching
  in later phases.
- **Self-contained caster geometry.** `world_bounding_sphere` is carried
  per-record for GPU-side spatial tests without requiring back-references into
  scene-prep.
- **Static/dynamic annotation.** `primitive_flags` carries
  `kStaticShadowCaster` and `kMainViewVisible` bits for CSM-6 static/dynamic
  splitting.

### 5.2 CSM-2 — Receiver Sample Foundation

**Status:** complete

Three GPU compute dispatches per frame:

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

The raw bounds are deliberately **not dilated** in this phase — they record
the precise visible-sample footprint. Dilation margins are stored alongside
for downstream phases (CSM-3, CSM-4) to apply contextually.

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

### 5.3 CSM-3 — Light-Space Receiver Mask

**Status:** complete

**Purpose:** Convert the tight receiver bounds from CSM-2 into a sparse
tile-granularity occupancy mask that enables fine-grained caster culling.

**Why the tight rect alone is not sufficient:**

The CSM-2 analysis produces a tight axis-aligned bounding rect in light space,
but it treats the entire rect as uniformly occupied. In practice, visible
receivers cluster in specific regions of the shadow map — e.g., ground plane,
nearby walls — leaving large regions of the tight rect unoccupied. A tile mask
captures this sparsity.

Five GPU compute dispatches per frame:

| Dispatch | Thread group | Grid | Purpose |
| - | - | - | - |
| `CS_ClearMasks` | 64×1×1 | `ceil(max_entries/64)` | Zero raw, base, hierarchy masks and count buffer |
| `CS_Analyze` | 8×8×1 | `ceil(W/8) × ceil(H/8)` | Per-pixel: classify into cascade, project to light space, mark raw tile via `InterlockedOr` |
| `CS_DilateMasks` | 64×1×1 | `ceil(jobs×base_tiles/64)` | Per-tile: read raw mask, apply conservative dilation radius, write base mask, count occupied tiles |
| `CS_BuildHierarchy` | 64×1×1 | `ceil(jobs×hier_tiles/64)` | Per-hierarchy-tile: OR-reduce base mask tiles, count hierarchy occupied tiles |
| `CS_Finalize` | 64×1×1 | `ceil(jobs/64)` | Per-job: write `ConventionalShadowReceiverMaskSummary` from CSM-2 analysis + tile counts |

#### CS_ClearMasks

Zeros all entries in raw mask, base mask, hierarchy mask, and count buffers.
The clear dispatch grid covers the maximum of all buffer sizes.

#### CS_Analyze

For each HZB mip-0 pixel:

1. **Load depth.** Skip sky pixels (`depth ≤ 0` in reversed-Z).
2. **Reconstruct world position.** Pixel → UV → NDC → clip → world via the
   inverse view-projection matrix.
3. **Compute eye depth.** `max(0, -(view_matrix × world_pos).z)`.
4. **Classify into cascade.** Same split logic as CSM-2 — first cascade uses
   `≥` at near boundary, subsequent use `>`.
5. **Project into light space.** `light_rotation_matrix × world_pos`.
6. **Quantize to tile coordinate.** Map the light-space XY position into the
   tile grid using `full_rect_center_half_extent` as the mapping domain.
7. **Mark tile.** `InterlockedOr(raw_mask[tile_index], 1)`.

**Mapping domain choice:** The tile grid is defined over the
`full_rect_center_half_extent` (the full cascade sphere-fit rectangle), not
the CSM-2 tight bounds. This means the grid covers the entire cascade
orthographic projection, and receiver occupancy within that grid reveals the
true spatial sparsity. Using the full rect also ensures the mask coordinate
system is stable and directly usable by CSM-4's caster bounding sphere tests
without coordinate remapping.

The per-pixel work mirrors CSM-2's `CS_Analyze` (world reconstruction,
cascade classification, light-space projection). The write is a single
`InterlockedOr` setting a tile to occupied — far cheaper than CSM-2's six
ordered-float atomic min/max operations.

#### CS_DilateMasks

Per base-mask tile:

1. Read the dilation radius from the CSM-2 analysis record's `xy_dilation_margin`
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

The hierarchy provides fast broad-phase overlap rejection for CSM-4. A caster
whose light-space projection does not overlap any occupied hierarchy tile
can be rejected without checking the full-resolution base mask.

#### CS_Finalize

Per job:

1. Read the CSM-2 analysis record and the tile counts from the count buffer.
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
CSM-2's per-pixel pass. The remaining four dispatches (clear, dilate,
hierarchy, finalize) are tile-count-proportional and negligible.

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

**Exit criteria (met):**

- Near cascades show materially sparse occupancy (occupied-tile ratio
  significantly below 1.0)
- No unexplained timing regression versus CSM-2 baseline

### 5.4 CSM-4 — GPU Caster Culling And Compaction

**Status:** complete

**Purpose:** Use the receiver analysis and mask to discard casters that cannot
produce visible shadows, and compact the survivors into a dense draw buffer
ready for counted-indirect raster.

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
   Test sphere center ± radius against the CSM-2 tight bounds
   (`raw_xy_min_max`) dilated by the XY dilation margin. Reject if fully
   outside.
4. **Broad phase B — dilated depth range (Z).** Test sphere center ± radius
   against the CSM-2 tight depth range (`raw_depth_and_dilation.xy`) dilated
   by the depth margin. Reject if fully outside.
5. **Medium phase — hierarchy mask overlap.** Compute the sphere's tile-grid
   footprint using `ComputeSphereTileBounds()` against the
   `full_rect_center_half_extent` mapping domain (same coordinate system as
   CSM-3). Divide by the hierarchy reduction factor and scan the hierarchy
   mask. Reject if no hierarchy tile overlaps.
6. **Fine phase — base mask overlap.** Scan the base mask tiles within the
   sphere's tile footprint. Reject if no base tile overlaps.
7. **Emit.** Atomically increment the per-job command count and append a
   `ConventionalShadowIndirectDrawCommand` into the partition's command
   buffer at `job_index × max_commands_per_job + slot`. Overflow is
   guarded by `max_commands_per_job`.

#### Partition model

The CPU collects contiguous draw-record runs grouped by `partition_index` from
the authoritative `CSM-1` stream. Each partition gets its own command buffer
and count buffer. This preserves the existing raster-pass partition structure
so CSM-5 can execute one `ExecuteIndirectCounted` call per (partition, cascade)
pair without restructuring the raster pass.

#### Coordinate system alignment

The tile-bounds computation uses `full_rect_center_half_extent` — the same
mapping domain used by CSM-3's `CS_Analyze` when building the mask. This
ensures the caster's tile footprint is directly comparable to the mask
without any coordinate remapping.

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
- After dispatch: command + count buffers → `kCommon`; job + partition →
  `kCommon`.
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
base) keeps per-thread work short. At this scale the pass will be invisible
in GPU timing. If draw counts grow to thousands, the thread model remains
efficient — one thread per (draw, cascade) with independent output slots.

#### Optional companion bounds (deferred)

If `CSM-1` world bounding spheres prove too coarse for mask overlap (i.e.,
many false survivors pass the mask test), an additional cull-bounds buffer
can provide tighter light-space AABBs per draw record. This extends `CSM-1`
without invalidating it. Currently deferred — sphere bounds are adequate
for the canonical benchmark.

**Exit criteria:**

- Non-trivial real-scene rejection (average eligible draws/job materially below
  the baseline full-record count)
- No unexplained CPU-path regression

### 5.5 CSM-5 — Counted-Indirect Conventional Raster (Planned)

**Status:** pending

**Purpose:** Execute only the compacted caster work, eliminating the current
CPU replay of full shadow partitions.

**Design:**

1. Convert `ConventionalShadowRasterPass` from direct CPU-driven draws to
   `ExecuteIndirectCounted` execution.
2. Reuse the same counted-indirect API and resource patterns already proven by
   the VSM raster path.
3. The indirect argument buffers are populated by CSM-4.
4. Each cascade's raster work is a single `ExecuteIndirectCounted` call.

**Exit criteria:**

- No full-partition direct replay on the hot path
- RenderDoc shows counted indirect execution
- Normalized shadow-pass GPU time is materially lower than the CSM-2 baseline

### 5.6 CSM-6 — Static / Dynamic Update Budget (Planned)

**Status:** pending

**Purpose:** If CSM-5 still exceeds the shadow budget, split static and
dynamic casters and apply explicit update budgeting.

**Design:**

- Use the `kStaticShadowCaster` flag already present in `CSM-1` draw records.
- Cache static shadow-map regions and invalidate only when the light direction
  or camera moves significantly.
- Re-render only dynamic casters every frame.
- Apply measured update budgeting only if timing evidence requires it.

**Contingency:** This phase is mandatory only if CSM-5 misses the performance
target. It may be skipped if CSM-5 already achieves the required GPU budget.

### 5.7 CSM-7 — Shadow Material / LOD Specialization (Planned)

**Status:** pending

**Purpose:** Remove remaining expensive low-value shadow work after structural
waste is eliminated.

**Design:**

- Shadow-only LOD policy where mesh LOD chains are available.
- Explicit alpha-tested shadow material budget controls.
- Validate alpha-tested correctness against the canonical capture scene.

### 5.8 CSM-8 — Closure

**Status:** pending

**Gate:**

- Normalized timing baseline repaired across all phases.
- Final conventional path uses receiver-sample analysis, receiver-mask culling,
  and counted-indirect raster.
- Release capture proves improvement against the canonical CSM-2 baseline.
- All design documentation is up to date.
- The invalidated receiver-object-sphere approach remains documented as rejected.

## 6. Key Implementation Details

### 6.1 Reversed-Z Depth Convention

Oxygen uses reversed-Z depth throughout:

- Near plane → depth `1.0`
- Far plane / sky → depth `0.0`
- The HZB "closest" texture uses `max()` reduction (closest to camera = largest
  depth value)
- The HZB "furthest" texture uses `min()` reduction

The CSM-2 analysis shader skips pixels where `depth ≤ 0.0` — this correctly
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

The CSM-2 finalize pass computes conservative dilation margins:

```cpp
xy_margin = max(world_units_per_texel,
                constant_bias + normal_bias + 2 * world_units_per_texel)
depth_margin = max(world_units_per_texel,
                   constant_bias + normal_bias)
```

These margins are stored in the analysis record but **not applied** to the raw
bounds. Downstream phases (CSM-3 mask dilation, CSM-4 culling tests) apply
them contextually. This separation keeps the raw analysis truthful and the
dilation policy tunable.

## 7. Invalidated Approach

The following approach was implemented, validated against the canonical
benchmark, and conclusively rejected:

**What it attempted:**

- Build per-cascade receiver volumes from `visible_receiver_bounding_spheres`
  (CPU-side whole-object bounds)
- Publish `ConventionalShadowCullJob` with a light-space rect + depth slab
  per cascade
- Test each `ConventionalShadowDrawRecord`'s bounding sphere against the
  receiver rect-slab
- Compact survivors on GPU

**Why it failed (compaction ratio = 1.000 on canonical benchmark):**

1. **Receiver truth was wrong.** Whole visible receiver objects are not the
   same as the visible receiver footprint.
2. **Bounds were too coarse.** Large architectural pieces (roofs, walls)
   expanded the receiver volume to approximately the full scene.
3. **Full-split geometry leaked.** The job geometry remained coupled to the
   full cascade slice.
4. **Predicate was too weak.** Sphere-vs-rect-slab is too conservative when
   both sides are whole-object bounds.
5. **Test gate was insufficient.** Synthetic contract tests validated
   mechanics, not real-scene selectivity.

This approach must not be reintroduced.

## 8. Validation Framework

Every phase must produce:

1. Release build evidence
2. Fresh canonical frame-350 capture at 50 fps
3. Sequential RenderDoc Python-script analysis
4. Explicit comparison against the authoritative CSM-2 baseline
5. Updated status in the remediation plan

Evidence is invalid if:

- Script errors were silently ignored
- Multiple RenderDoc analyses ran in parallel
- The benchmark-settings swap protocol was not fully restored

Structural phases may close without GPU-time reduction if they show no
structural regression and the expected GPU data product is present and
analyzed.

Performance phases must additionally show normalized timing improvement and
draw/job reduction.
