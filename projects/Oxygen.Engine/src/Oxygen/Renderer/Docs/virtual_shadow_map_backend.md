# Virtual Shadow Map Backend Technical Specification

Purpose: define the concrete backend contract and implementation shape for
`VirtualShadowMapBackend` so virtual shadows land on the existing shared shadow
system without reopening renderer-shader interface work.

Cross-references: [shadows.md](shadows.md) |
[implementation_plan.md](implementation_plan.md) |
[passes/design-overview.md](passes/design-overview.md) |
[renderer_shader_interface_refactor.md](renderer_shader_interface_refactor.md)

This document is intentionally more concrete than [shadows.md](shadows.md). It
locks the backend-level choices needed to implement directional virtual shadow
maps in Oxygen on top of the already remediated shadow architecture.

## 1. Scope and Decisions

This specification defines the first production implementation of the virtual
shadow-map family in Oxygen.

Locked decisions:

- Initial shipping virtual backend scope is **directional lights only**.
- The current runtime slice publishes **one effective directional VSM source
  per view**; a synthetic sun takes that slot ahead of scene sun-tagged
  directionals, while additional non-sun shadowed directionals stay on the
  conventional path.
- Directional virtual coverage uses **camera-relative light-space clipmaps**.
- Virtual pages are backed by a **shared physical atlas pool**, not by one
  `Texture2DArray` layer per page.
- Receiver-driven page requests are generated **after main depth pre-pass**,
  then virtual page rendering happens **before main shading**.
- Shading dispatch remains rooted in `ShadowInstanceMetadata`; virtual shadow
  maps do not add a separate lighting path.
- Shadow-only caster routing remains on the existing ScenePrep/draw-metadata
  path; virtual page rendering does not introduce a second scene extraction
  pipeline.

Not locked here:

- Spot/point virtual shadow products
- contact-shadow integration details
- filter upgrades beyond the baseline comparison-PCF path

## 2. Backend Responsibilities

`VirtualShadowMapBackend` owns:

- directional virtual product planning
- clipmap coverage selection and snapping
- physical page allocation and eviction
- page table publication
- request generation resources
- dirty-page tracking
- page update scheduling and budgeting
- shader-visible virtual addressing metadata
- debug counters and residency telemetry

`ShadowManager` remains responsible for:

- family selection
- top-level product scheduling
- deterministic budgeting across families
- shared `ShadowFrameBindings` publication
- coordination between conventional and virtual backends

The backend must not own light collection, main-pass lighting, or a separate
draw-routing system.

## 3. Core Directional Model

Directional virtual shadows are implemented as a set of nested clip levels in
light space.

### 3.1 Clip levels

Default directional virtual coverage uses:

- 6 clip levels
- each level covers double the world-space span of the previous level
- clip level 0 is the finest near-camera directional region
- clip level 5 is the coarsest far region

Each clip level is:

- centered relative to the active view
- oriented in the directional light's light space
- snapped in light-space page increments for stability

The clip-level origin must not move continuously with the camera. It moves only
when the camera crosses a page-aligned threshold in light space.

### 3.2 Virtual page grid

Each clip level owns a virtual page grid.

Default starting policy:

- 64 x 64 virtual pages per clip level
- page interior resolution: 128 x 128 texels
- physical page border: 2 texels on each side
- physical stored page dimensions: 132 x 132 texels

Rationale:

- `128` interior texels aligns with common production VSM practice
- a `2`-texel border supports the current comparison-PCF kernels without edge
  sampling artifacts
- `64 x 64` pages per level is large enough to give useful effective
  directional coverage without exploding request/update cost

These values remain quality-tier configurable, but the page addressing contract
assumes a fixed page interior size within a run.

Current runtime note:

- the live implementation no longer hard clamps physical page size to `256`
  texels
- the active runtime is now correctness-first:
  - virtual page-grid density is fixed per quality tier
  - physical atlas capacity is fixed per quality tier
  - frame-budget-shaped page selection is deferred until after correctness and
    quality are closed
- the live implementation now also decouples virtual address-space density from
  physical residency capacity:
  - higher tiers use more virtual clip levels and denser page grids than the
    initial validation slice
  - the physical atlas is capacity-bounded and no longer sized as if every
    virtual page had to be resident simultaneously
  - when virtual clip count exceeds authored conventional cascade count, the
    extra clip boundaries are generated from the practical split policy instead
    of indexing past authored cascade distances
- directional VSM clipmap coverage now follows a stable exponential ladder:
  - the base page world size is quantized to a power-of-two step
  - each clip level doubles page world size and clip coverage
  - the ladder is no longer regenerated from a coarse cascade-like set of
    per-view extents, because that produced large visible quality wedges and
    unstable far-distance refinement
- request generation is footprint-driven:
  - the request producer chooses the coarsest containing clip whose logical
    texel size still matches the receiver footprint
  - it requests that clip plus an optional finer prefetch clip near the
    threshold
  - guaranteed coarser fallback comes from the backend-owned coarse clip
    backbone, not from redundantly emitting every coarser clip into feedback
- shading is not footprint-driven:
  - shading samples the finest available containing clip first
  - if that page is invalid, it falls back coherently to coarser clips
  - clip-boundary blending remains between the active fine clip and the next
    coarser clip

### 3.3 Coverage selection

For each directional virtual product:

1. Build light rotation from the light direction.
2. Build camera-relative clip centers in world space.
3. Transform those centers to light space.
4. Snap each clip origin in page-sized light-space increments.
5. Publish per-level scale/origin metadata to shaders.

The backend must prefer stable snapped clip coverage over aggressive per-frame
 rectangular fitting. Stability is the baseline requirement.

## 4. Physical Resource Model

### 4.1 Physical page pool

The physical page pool is a shared atlas-backed depth resource.

Recommended initial layout:

- format: `D32`
- texture type: `Texture2DArray`
- atlas layer size: `8192 x 8192`
- tile size per page: `132 x 132`
- pages per atlas layer: `floor(8192 / 132)^2 = 61 x 61`
- usable pages per atlas layer: `3721`

The backend may own multiple atlas layers/pagesets according to tier policy.

Each physical page is identified by:

- `atlas_index`
- `tile_x`
- `tile_y`

The shader-visible sample transform for one page is:

- atlas layer index
- physical UV scale
- physical UV bias

### 4.2 Page table

The shader-visible page table is a bindless resource that maps:

- `shadow_instance`
- `clip_level`
- `page_x`
- `page_y`

to:

- valid bit
- physical atlas index
- physical tile coordinates

The page table must be GPU-readable by shading and GPU-writable by residency
/update passes.

Recommended first implementation:

- one `RWTexture2DArray<uint>` or equivalent typed UAV/SRV page table
- one array slice per clip level per active directional virtual product
- 32-bit packed entry for shader-visible residency/addressing

Packed entry baseline:

- bits 0..11: physical tile x
- bits 12..23: physical tile y
- bits 24..27: atlas index
- bit 28: valid
- bit 29: requested this frame
- bits 30..31: reserved

If later capacity exceeds this packing, switch to a structured-buffer entry
format without changing top-level `ShadowInstanceMetadata`.

### 4.3 Backend-private page state

The backend needs a richer CPU/GPU-internal page-state table than the
shader-visible page table.

Per virtual page state includes:

- page key `(product, clip_level, page_x, page_y)`
- current physical allocation
- last touched frame
- last fully rendered frame
- dirty bit
- pending render bit
- eviction lock / protected bit for currently requested pages

This state is not part of the shading contract.

## 5. Renderer Contracts

### 5.1 Shared shadow product model

Virtual products continue to use the existing top-level shadow contract:

- `ShadowInstanceMetadata`
- directional or local shadow metadata
- family-specific addressing resources published through `ShadowFrameBindings`

The renderer must not add a separate "virtual directional light buffer" for
shading.

### 5.2 Required shader-visible additions

`ShadowFrameBindings` must grow virtual-family resources:

- `virtual_shadow_page_table_slot`
- `virtual_shadow_physical_pool_slot`
- `virtual_shadow_directional_metadata_slot`
- `virtual_shadow_feedback_slot`
- `virtual_shadow_debug_slot`

The exact slot naming can be refined, but ownership stays shadow-local.

### 5.3 Directional virtual metadata

The virtual directional backend needs a directional-family payload separate from
the conventional CSM payload.

Suggested contract:

```cpp
struct DirectionalVirtualShadowMetadata {
  uint32_t shadow_instance_index;
  uint32_t clip_level_count;
  uint32_t page_table_base_slice;
  uint32_t flags;

  glm::vec4 clip_origin_ls[6];
  glm::vec4 clip_page_world_size[6];
  glm::vec4 clip_world_to_virtual_scale_bias[6];
};
```

Meaning:

- `clip_origin_ls`: snapped clip origin in light space
- `clip_page_world_size`: world/light-space meters covered by one page
- `clip_world_to_virtual_scale_bias`: transforms light-space XY into page-table
  coordinates

The existing directional metadata may either:

- point to this payload through an implementation-specific payload index, or
- be extended with an implementation payload index field

The important rule is unchanged: top-level shading starts from
`ShadowInstanceMetadata`.

## 6. Frame Pipeline

The virtual directional flow for one frame is:

1. `ShadowManager` selects the virtual family for eligible directional
   products.
2. `VirtualShadowMapBackend` builds/updates clipmap coverage metadata.
3. Main depth pre-pass runs.
4. The first visual-validation slice computes a deterministic camera-relative
   clipmap working set on the CPU.
5. Every page in that working set is mapped into the physical pool and turned
   into a raster shadow job.
6. Virtual shadow page raster passes render those pages.
7. `ShadowFrameBindings` publishes page tables, physical pool, and virtual
   metadata.
8. Main shading samples shadows through `ShadowInstanceMetadata`.

After the first visual-validation slice:

- a virtual page-request pass scans visible receivers from the main depth
- a GPU resolve/update pass consumes that request signal directly, deduplicates
  requests, and applies reuse / allocation / eviction decisions
- that same resolve stage emits the compact page-raster schedule so raster
  consumes one authoritative resolved-page contract rather than a parallel
  legacy CPU job list
- dirty/newly allocated pages are rasterized from that resolved schedule
  instead of the whole fixed working set

Important consequence:

- Unlike the current conventional directional path, virtual directional page
  rendering is scheduled after main depth pre-pass so the later sparse
  receiver-driven request path can land without moving the backend again.

That is acceptable because the shadow architecture is already backend-aware and
does not require conventional and virtual families to share identical pass
placement.

## 7. Request Generation

### 7.1 Receiver-driven requests

The final sparse VSM path is receiver-driven. The live implementation is still
an intermediate slice, but the corrected algorithm is now fixed:

- depth/feedback-driven fine-page refinement is the only steady-state request
  source
- a mandatory coarse clipmap backbone covers the visible frustum footprint
  every frame
- fine-page misses must degrade to valid coarser clip coverage by
  construction, not by heuristic luck
- mixed CPU receiver-bounds requests are not part of the steady-state
  algorithm because they make correctness and troubleshooting ambiguous

Current intermediate behavior:

- `VirtualShadowMapBackend` computes one camera-relative clipmap grid per level
- the visible frustum footprint is projected into clip pages to build the
  mandatory coarse clip backbone
- per-view depth feedback refines finer clip pages on top of that backbone
- requested pages are resolved into contiguous per-clip regions
- coarse backbone coverage is mapped first, then fine refinement coverage
- when physical tile capacity is exhausted, the planner must still preserve
  valid coarse clip coverage before spending tiles on fine refinement
- the shader-visible page table is populated only for the selected resident
  pages and is now backed by a persistent per-view GPU buffer with a stable
  bindless slot
- each publish stages current page-table contents for pass-time copy into that
  persistent GPU resource before virtual page raster / later shading
- March 12, 2026 regression fix: the resolve-owned page-table rebuild must
  repopulate that persistent upload buffer before `PreparePageTableResources`
  performs the GPU copy; otherwise the physical atlas can contain fresh pages
  while scene shading still samples an all-zero page table
- backend-private resolve-state now also stages a deterministic resident-page
  snapshot plus resolve counters into persistent per-view GPU buffers with
  stable bindless slots; this is a bridge surface for the upcoming GPU
  resolve/update pass, not the final end-to-end resolve path yet
- a live `VirtualShadowResolvePass` now runs after request generation,
  compacts currently mapped requested pages into per-view GPU schedule
  buffers, and readbacks a CPU-visible `VirtualShadowResolvedRasterSchedule`
  bridge payload
- March 12, 2026 step-4 bridge slice: that same resolve pass now explicitly
  executes the current-frame CPU residency / allocation / page-table
  preparation before later upload and raster consumption, so
  `PreparePageTableResources` no longer recomputes current-frame page-table
  state on demand
- March 12, 2026 hardening slice: `TryGetVirtualRenderPlan` and
  `TryGetVirtualViewIntrospection` are now observation-only exports. Runtime
  passes and focused tests explicitly call `ResolveVirtualCurrentFrame`
  before reading them, so current-frame allocation/page-table work no longer
  hides behind getter side effects
- March 12, 2026 ownership slice: `VirtualShadowResolvePass` now prepares the
  current-frame page table resources for every virtual frame before any later
  raster/debug consumer runs, even when no GPU request dispatch is active.
  `VirtualShadowPageRasterPass` and `VirtualShadowAtlasDebugPass` now read the
  published exports without re-resolving state themselves
- March 12, 2026 cleanup slice: `MarkRendered` is now post-raster bookkeeping
  only and no longer backdoor-resolves current-frame allocation/page-table
  state. The cached-vs-reused atlas-debug regression now explicitly resolves
  the first frame before marking it rendered, matching the new ownership split
- March 12, 2026 startup gating slice: `VirtualShadowRequestPass` and
  `VirtualShadowResolvePass` now skip startup / transition frames until the
  current scene view has live prepared draw metadata and non-empty partitions.
  That closes the earlier startup ordering hole where resolved virtual pages
  could be published before the scene was ready to raster shadow casters
- March 12, 2026 step-4 completion slice: publish-time VSM view-state
  construction now only snapshots the authoritative prior resident state.
  Current-frame carry, dirty marking, tile release, allocation, eviction, and
  page-table mutation now all happen inside `ResolvePendingPageResidency`, and
  unresolved republishes retain the last resolved resident snapshot until that
  explicit resolve stage consumes it
- March 12, 2026 step-5 hardening slice: eviction under physical-pool pressure
  now prefers invalid resident pages before unrelated clean cached pages. The
  focused regression
  `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
  fills the low-tier pool, moves a large caster to create pressure, and proves
  an excluded clean page for a still-visible caster survives without reraster
- the resolve-to-raster handoff now also materializes the current-frame
  resolved-page raster contract from backend-private pending jobs just before
  raster consumption; this removes the old publish-time duplication of a
  second raster schedule without changing allocation ownership yet
- design correction on March 12, 2026: that bridge payload is observational
  only because it compacts requests that already resolve to valid current-frame
  page-table entries; until resolve owns current-frame allocation and raster
  scheduling end-to-end, the backend must not use it to prune CPU-authored
  fine pending jobs
- snapped-identical page tables and clip metadata reuse resident pages across
  frames and skip redundant page rerasterization
- that first slice activates only when the view resolves to exactly one active
  directional shadow product; views with multiple directional products stay on
  the conventional family

This preserves the final top-level renderer/shader contracts:

- `VirtualShadowMapBackend`
- shadow-local page tables
- shadow-local physical page pool
- family dispatch through `ShadowInstanceMetadata`
- virtual shadow rasterization after the main depth pre-pass

What remains in progress after that slice:

- live request dedup / residency resolve / allocation still happens on the CPU
  after feedback readback; there is not yet a GPU-side resolve/update pass for
  those decisions
- the explicit resolve stage now owns the live CPU-side current-frame
  carry, dirty marking, tile release, allocation, eviction, and page-table
  mutation; publish/getter/mark-rendered/raster/debug paths no longer mutate
  residency outside that stage, but final GPU-side ownership is still missing
- `MarkRendered` no longer re-enters that resolve stage implicitly; it only
  finalizes already-resolved pending pages after raster, which keeps the
  resolve/pass ownership boundary single-sourced
- page-table upload preparation is now resolve-owned at pass time, so raster
  and atlas-debug consumers no longer duplicate current-frame resolve/page-table
  work; the remaining ownership gap is still the CPU-authored allocator /
  eviction / mutation logic behind that resolve stage
- the observation-only getter cleanup removed implicit current-frame resolve
  side effects, and the startup gating slice now prevents request/resolve from
  running before prepared draw metadata is live, so the short `RenderScene`
  smoke no longer shows resolved pages on `draw_bytes=0` / `partitions=0`
  frames
- virtual page raster now consumes a dedicated resolved-page raster contract,
  and the resolve handoff now materializes that current-frame contract from
  backend-private pending jobs and per-page view constants; the readback
  `VirtualShadowResolvedRasterSchedule` bridge is still telemetry only today,
  and the final raster contract is still not fully GPU-owned
- the new persistent resident-page snapshot and resolve counters are bridge
  resources only; they do not yet remove the CPU planner from the live path
- partial page updates beyond the current bounded raster planning
- deterministic eviction under residency pressure at the full final model

The first slice is deliberately not presented as a complete sparse-residency
implementation.

Current backend note:

- runtime now tracks explicit resident-page state (`PendingRender` and
  `ResidentClean`) plus `last_touched_frame`; this replaces the old single
  validity boolean and gives the backend the right ownership surface for later
  dirty-page and eviction work
- clean unrequested pages now remain resident across publishes and are evicted
  only when a new request needs their tile; current eviction ordering is
  deterministic per view: unrequested pages only, coarser clip first, then
  oldest `last_touched_frame`, then stable page index
- retained clean pages are cache-only and must not remain mapped in the current
  frame page table; active mapped coverage is now tracked separately from total
  resident pages

The sparse request generator is receiver-driven.

Inputs:

- main depth buffer
- view/projection data
- active directional virtual products
- optional receiver normal buffer when later quality work needs it

For each sampled receiver location:

1. reconstruct world position
2. test whether the active directional virtual product is relevant
3. transform to the product's light space
4. choose the footprint-selected clip level: the coarsest containing clip whose
   logical texel size still matches the receiver footprint
5. compute `(page_x, page_y)` for that level
6. mark that clip and optionally one finer prefetch clip; the C++ backend keeps
   the mandatory coarse fallback backbone resident separately

The sparse request pass may be full-resolution or downsampled; the first
shipping sparse path should start with a downsampled compute pass to bound
request traffic.

### 7.2 Request deduplication

Requests must be deduplicated before page allocation.

Recommended initial sparse-residency method:

- a request-bit texture/UAV matching the page-table footprint per clip level
- atomic OR / exchange to mark requested pages
- a compact pass then scans requested bits into a linear request list

This is deterministic and avoids append-buffer duplicates exploding under dense
geometry.

Current live note:

- `VirtualShadowRequestPass` now performs the first real runtime request
  generation step after `DepthPrePass`
- it writes a GPU deduplicated request-bit mask and copies it into a
  per-frame-slot readback buffer
- on safe slot reuse, the CPU decodes that mask and submits per-view request
  feedback to `VirtualShadowMapBackend`
- compatible feedback is the only steady-state fine request source
- feedback-driven requests now carry a bounded page guard band so fine virtual
  coverage does not disappear exactly at the camera frustum edge
- feedback-driven requests now build stable per-clip requested regions instead
  of feeding sparse per-page hits directly into selection
- feedback now carries only the footprint-selected clip and optional finer
  prefetch clip; the mandatory coarse clip backbone remains backend-owned so
  feedback does not bloat the key set with redundant coarse pages
- the planner must combine those regions with a mandatory coarse clip backbone
  so oversubscription cannot leave fine pages without valid coarser fallback
- virtual pages-per-axis stays fixed per quality tier; correctness is preserved
  by deterministic physical-tile selection, not by shrinking the virtual
  address space under pressure
- until the first safe feedback arrives for a view, the runtime uses the same
  coarse-backbone algorithm without fine refinement instead of switching to a
  second request source

## 8. Residency and Invalidation

### 8.1 Page state machine

Each virtual page transitions through:

- `Unmapped`
- `ResidentClean`
- `ResidentDirty`
- `PendingRender`

Rules:

- newly allocated pages begin `PendingRender`
- invalidated resident pages become `ResidentDirty`
- scheduled dirty pages become `PendingRender`
- completed pages become `ResidentClean`

### 8.2 Deterministic priority

Page allocation and update order must be deterministic.

Priority key:

1. shadow product priority from `ShadowManager`
2. clip level priority (finer first)
3. current-frame requested bit
4. page distance to view center within the clip
5. stable page key tie-breaker

Eviction priority is the inverse:

1. not requested this frame
2. coarser levels first
3. oldest `last_touched_frame`
4. stable page key tie-breaker

### 8.3 Invalidation sources

Directional virtual pages are invalidated by:

- directional light transform or direction change
- authored shadow setting changes
- quality-tier changes affecting page tables or clip counts
- geometry/LOD changes for casters overlapping the clip coverage
- transform changes for casters overlapping the clip coverage
- material-domain shadow participation changes

The backend should invalidate conservatively but spatially:

- transform/bounds changes produce dirty volumes in world space
- dirty volumes are transformed into light space
- overlapping clip levels/pages are marked dirty

*Future Evolution (Static/Dynamic Separation):*
While the initial implementation invalidates pages entirely when overlapping dirty volumes, future production hardening for dense scenes should decouple static and dynamic geometry caches. This avoids massively rerasterizing static background geometry when a dynamic caster moves through a page. The invalidation model must eventually track static vs. dynamic page state and composite dynamic geometry into long-lived static cache pages.

Do not invalidate the whole product unless:

- light basis changes fundamentally
- quality tier changes page table dimensions
- page-table layout changes

## 9. Page Rasterization

### 9.1 Job model

Virtual shadow page rendering should reuse the generic raster shadow pass
machinery through a page-specific job type, not through a second draw-submission
system.

A virtual raster job minimally needs:

- target atlas layer
- target tile rect
- `ViewConstants` snapshot for that page
- shadow product / payload reference
- pass domain policy

This implies a future `RasterShadowTargetKind::kAtlasRect` in addition to the
current array-slice target kind.

*Future Evolution (Compute Rasterization):*
Virtual page rendering is uniquely punishing to hardware fixed-function rasterizers due to the high density of small triangles falling into small (e.g., 128x128) page viewports. The virtual raster job model and pass design must remain flexible enough to eventually accept a compute-shader software rasterization backend for small triangles, which is standard in SOTA virtual shadow map implementations.

### 9.2 Page projection

Each page renders an orthographic light-space subregion of a clip level.

For one page:

1. derive the page's light-space XY bounds from clip origin + page coordinates
2. derive near/far from overlapping caster bounds, with deterministic minimum
   padding
3. build the page view-projection
4. render shadow casters into the physical tile rect

The same bias/filter ownership rules as conventional raster shadow rendering
still apply.

### 9.3 Border handling

Because physical pages store a border, page rasterization must render the
interior plus border coverage.

This prevents comparison-PCF taps from sampling unrelated page contents at tile
edges.

## 10. Shader Sampling

### 10.1 Selection

Shading begins at `ShadowInstanceMetadata`.

For a directional virtual product:

1. reconstruct or receive world position
2. transform into light space
3. choose the finest clip level whose coverage contains the point
4. compute virtual page coordinates
5. look up the shader-visible page table entry
6. if invalid, fall back to the next coarser clip level
7. sample the physical atlas page and perform comparison filtering

The fallback order is always finer-to-coarser and deterministic.

### 10.2 Clip boundary continuity

Nested clip levels need explicit continuity handling.

Required behavior:

- stable finest-valid level selection
- narrow blend band near clip boundaries
- deterministic coarse fallback when the finer level page is invalid

The shader must not perform arbitrary "search every level and pick something"
logic that causes view-angle-dependent hopping.

### 10.3 Filtering

Baseline virtual filtering mirrors the conventional default:

- comparison-based filtering
- kernel-aware border use
- quality-tier-controlled kernel size

The virtual backend must not require a different lighting contract just to
change filtering policy.

## 11. Quality Tiers

Initial directional virtual defaults:

| Tier | Clip Levels | Page Grid / Level | Page Interior | Physical Atlas Budget |
| --- | --- | --- | --- | --- |
| Low | 4 | 32 x 32 | 128 | 1 atlas layer |
| Medium | 5 | 48 x 48 | 128 | 1 atlas layer |
| High | 6 | 64 x 64 | 128 | 2 atlas layers |
| Ultra | 6 | 64 x 64 | 128 | 4 atlas layers |

Per-frame update budgets are intentionally not part of the current correctness
path. The active implementation uses fixed quality-tier resources and a
deterministic selection order rather than frame-budgeted density scaling.

Current runtime note:

- the current validation slice is bounded by fixed quality-tier resources, but
  not by frame-budget density capping or budget-driven backend fallback
- page requests are generated from runtime depth feedback when available;
  otherwise bootstrap coverage comes only from the mandatory coarse clip
  backbone and visible receiver bounds used to build contiguous refinement
  regions before the first safe feedback arrives
- `VirtualShadowRequestPass` is now the live runtime producer for that feedback
  path; it runs after `DepthPrePass`, scans the main depth buffer, and submits
  bounded-lifetime request feedback through a slot-safe readback path
- snapped-identical clip metadata and page tables reuse resident physical pages
  across frames and skip redundant page rerasterization
- resident-page reuse is only content-valid when directional shadow inputs and
  caster inputs are unchanged; otherwise the backend reuses physical tiles but
  rerasterizes the requested pages
- caster-input equality must not rely on coarse bounds alone; the live runtime
  invalidation key also includes a prepared shadow-caster content hash derived
  from shadow-caster draw metadata and world transforms
- allocated physical tiles are not treated as valid shadow contents until the
  virtual page-raster pass completes; same-frame republishes preserve pending
  raster jobs instead of incorrectly clearing them
- the virtual page-raster pass clears only the page rectangles being updated;
  clearing the entire physical atlas would destroy reused resident pages and is
  not a valid runtime behavior
- virtual pages must reserve filter guard texels around the logical page
  interior; shading samples the logical interior while PCF taps are allowed to
  consume the guard texels, avoiding visible seams between independently
  rasterized pages
- virtual receiver bias/filter sizing must use the logical interior texel size
  after guard reservation; using the full physical page resolution
  underestimates virtual texel scale and leaves projected striping on large
  flat receivers
- virtual page ownership and the full in-page filter footprint must stay on
  the un-biased receiver position; only the depth comparison uses the biased
  receiver position, so bias cannot translate the sampled shadow laterally
  away from the caster base
- virtual comparison-PCF must apply receiver-plane depth bias per tap; reusing
  one receiver depth for the whole kernel reintroduces regular self-shadow
  striping on sloped receivers
- virtual comparison-PCF should resolve neighboring taps through the page table
  instead of clamping the whole kernel inside the center page; otherwise the
  virtual page lattice can still appear as soft periodic bands across large
  receivers
- virtual page rasterization should keep a non-zero but tightly clamped
  slope-scaled depth bias; leaving the rasterizer slope term at zero pushes
  all acne prevention onto receiver-side bias and allows regular moire bands
  to survive on broad receivers and caster faces
- requested coverage resolves into contiguous per-clip regions:
  - coarse backbone clips are mapped first every frame
  - fine refinement regions from feedback or bootstrap bounds are mapped after
    that in coarse-to-fine order
  - this keeps valid coarse fallback coverage alive even when fine refinement
    cannot map every requested page
- invalid fine pages currently fall back to the next valid coarser clip in
  shading so bounded residency remains visually coherent
- `virtual-only` remains the strict validation mode and does not use the
  `prefer-virtual` fallback to mask backend viability problems

## 12. Debugging and Validation

Required visual/debug support:

- clip level visualization
- requested pages
- resident pages
- dirty pages
- atlas occupancy
- physical page age / reuse
- page-update counts and budget overruns
- fallback-to-coarser-level visualization in shading

Required automated coverage:

- page-table packing/layout parity between C++ and HLSL
- deterministic request deduplication
- stable clip snapping
- invalidation from caster dirty volumes
- deterministic eviction ordering
- shader-visible invalid-page coarse fallback

## 13. Implementation Sequence

The frozen implementation order for the current directional slice is:

1. publish and consume one authoritative resolved-page raster contract
   (`landed`)
2. keep the persistent backend-private GPU request / residency state buffers
   and explicit resolve-stage handoff into that contract (`landed`)
3. centralize current-frame resolve/update ownership in the explicit resolve
   stage (`landed`, still CPU-authored)
4. remove the parallel legacy raster-job path so raster consumers read only the
   resolved-page contract (`landed`)
5. expand debug and automated validation around request, residency, and
   raster-schedule coherence (`landed`)
6. finish live interactive validation in `RenderScene` and Sponza (`remaining`)

Only after the above is stable:

- add family selection policy to actively choose virtual vs conventional for
  directional products
- then expand to spot/point virtual products

### 13.1 Next Milestone

The next implementation milestone is:

`Directional VSM Production Candidate`

This milestone is complete only when:

- depth/feedback-driven request generation is live
- request deduplication, residency updates, and raster-page scheduling are
  driven from one authoritative resolve path
- residency reuse/invalidation/eviction are deterministic and content-safe
- `virtual-only` is stable under camera rotation/translation in validation
  scenes including Sponza
- directional VSM edge quality is no longer visibly below the conventional
  directional path in normal validation scenes
- the backend exposes the required debug surfaces for request, residency,
  fallback, and budget diagnosis

This is the correct next bar before any expansion to local-light virtual
shadows.

Implementation status, March 12, 2026 contract cleanup:

- the earlier scope correction was valid: resolve-to-raster contract
  convergence was required, not just another allocation pass
- live code now publishes only `VirtualShadowResolvedRasterPage` to virtual
  raster consumers; the parallel legacy raster-job export path is removed
- current-frame resolve/update ownership remains CPU-authored inside the
  explicit resolve stage
- user-reported manual step-6 validation later confirmed functional stability
  in `RenderScene` and Sponza
- status remains `in_progress` until the conventional-parity quality bar is
  explicitly signed off

Implementation update, March 12, 2026 bridge slice:

- persistent backend-private GPU residency snapshot buffers are now in code:
  - a deterministic resident-page snapshot is staged per view
  - per-view resolve counters are staged alongside it
  - both resources keep stable bindless slots across republishes of the same
    view
- this is intentionally still a bridge slice:
  - the live resolve pass now owns the current-frame handoff into the
    resolved-page raster contract, but the readback bridge payload remains
    telemetry only for request compaction introspection
  - the CPU still owns allocation decisions and the underlying pending-job
    authoring that feeds that handoff
- focused validation in `build-vs`:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStateSnapshotTracksResidentPagesDeterministically`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStateUsesStablePerViewGpuBuffers`
  - `LightManagerTest.ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract`
  - `*ShadowManagerPublishForView_Virtual*`
  all passed
- March 12, 2026 step-5 hardening update:
  - focused invalidation/regression coverage now spans camera-motion clip
    shifts, caster persistence, snap-boundary feedback invalidation,
    depth-remap invalidation, and budget pressure
  - validation in `build-vs`:
    - `LightManagerTest.ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
    - `*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`
    - `Oxygen.Examples.RenderScene.exe --frames 8 --fps 100 --directional-shadows virtual-only`
    all passed
  - step 5 is closed; the later user-reported step-6 live validation closed
    the frozen execution-plan gate, but overall status remains `in_progress`
    until conventional-parity quality is explicitly signed off

Implementation status, March 10, 2026:

- `in_progress`
- Review correction, March 10, 2026:
  - the earlier cache/invalidation close-out was too optimistic
  - CPU-side resident-key, reuse, eviction, and spatial invalidation work is
    still present in code, but the directional runtime is not yet at the
    intended stability/performance bar because:
    - request generation and shading still disagree on which directional clip
      should be sampled for a given receiver footprint
    - feedback-driven fine demand is still expanded into one rectangular region
      per clip, which over-selects pages in dense scenes
  - until those two runtime gaps are corrected and visually revalidated, this
    slice must remain treated as `in_progress`
- Update, March 10, 2026 follow-up:
  - directional lighting now starts from the same footprint-selected clip
    family that the request pass selects, with optional finer-clip blending and
    coarser fallback only when needed
  - feedback-driven fine demand now stays sparse per page with bounded local
    dilation instead of inflating into one rectangular region per clip
  - automated validation is green again in `build-vs`, but overall status stays
    `in_progress` until visual validation confirms the runtime behavior in
    `RenderScene`/Sponza
- Update, March 10, 2026 debug instrumentation correction:
  - the temporary shader-side page-line debug path has been removed again
  - directional VSM shading is back on the normal visibility path instead of
    forcing no-op visibility
  - request-footprint fixes remain in code, but the active runtime debug
    surface is now the floating `RenderScene` atlas inspector rather than GPU
    lines in the shading path
  - overall status remains `in_progress` until the atlas window is visible in
    scene and validated against static-scene captures
- Update, March 10, 2026 atlas inspector correction:
  - the line-overlay approach above did not provide a trustworthy enough view
    of physical page residency/churn for the remaining runtime failures
  - the active debug scope is now a floating `RenderScene` ImGui window backed
    by a compute-generated `RGBA8` texture derived from the physical VSM atlas
  - the atlas-debug compute pass must run after virtual shadow page raster so
    the inspector reflects the same physical pool contents used by the current
    frame
  - the D3D12 ImGui backend must expose that texture through its dedicated
    shader-visible heap rather than assuming the engine bindless SRV heap can
    be passed directly as `ImTextureID`
  - overall status remains `in_progress` until the atlas window is visible in
    `RenderScene` and confirmed against a static-scene capture
- Update, March 10, 2026 line-debug removal follow-up:
  - the temporary shader-side page-line debug path is fully removed from the
    active directional VSM shaders
  - `RenderScene` now builds again with the floating atlas-inspector hookup via
    the concrete `ForwardPipeline` and the D3D12 ImGui texture-registration
    bridge
  - current `build-vs` validation:
    - shader target build passed
    - `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*` -> `22/22 passed`
    - `Oxygen.Renderer.LightManager.Tests.exe` -> `34/34 passed`
  - runtime visual validation of the atlas window is still pending, so overall
    status remains `in_progress`
- Update, March 10, 2026 cache-churn correction:
  - atlas-inspector captures showed that static-scene camera motion was still
    recreating too much of the physical pool
  - one VSM-local cause was order-sensitive invalidation: the backend hashed
    and pairwise compared `shadow_caster_bounds` in collected-item order, which
    can change when camera motion reclassifies submeshes between main-view and
    shadow-only participation
  - one renderer-level cause was order-sensitive coarse content hashing:
    `HashPreparedShadowCasterContent` hashed shadow-caster draws in view-sorted
    order, so camera motion changed the content hash even when caster content
    did not
  - both paths are now canonicalized in code; overall status remains
    `in_progress` until `RenderScene` confirms bounded atlas churn under
    static-scene camera movement
- Update, March 10, 2026 feedback/cache scope correction:
  - runtime atlas captures showed that the earlier feedback realignment was
    still insufficient for directional camera motion
  - feedback is still stored as local page indices plus an origin-sensitive
    directional lattice hash, so page-aligned clip-origin shifts can discard
    still-reusable fine-page demand
  - when feedback is dropped, the fine-clip bootstrap path still merges all
    visible receiver bounds into one clip-wide rectangle, which can saturate
    the atlas and evict useful clean pages even in a static scene
  - the corrective implementation scope is now:
    - store feedback as absolute resident-page keys
    - match feedback on directional address space, not clip origin
    - keep current-frame bootstrap sparse per receiver
  - status remains `in_progress` until that path is implemented and validated
    in `build-vs`
- Update, March 10, 2026 resident-key feedback and sparse bootstrap
  correction:
  - `VirtualShadowRequestPass` now converts GPU request bits into absolute
    resident-page keys using the clip-grid origins from the metadata snapshot
    that produced the request mask
  - request-feedback compatibility now keys off directional address space
    instead of clip origin, so page-aligned clip-origin motion keeps reusable
    fine-page demand alive
  - fine current-frame bootstrap now marks per-receiver regions directly
    instead of merging all visible receivers into one dense clip rectangle
  - new LightManager regressions now cover:
    - resident-key reuse across clip-origin motion
    - sparse bootstrap preservation with a gap between far-apart receivers
  - `build-vs` validation evidence:
    - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*` -> `25/25 passed`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe` -> `37/37 passed`
    - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - overall feature status remains `in_progress` until `RenderScene` confirms
    the atlas/shadow behavior during live camera motion
- The current runnable slice is now in code:
  - backend resource ownership
  - directional virtual metadata publication
  - virtual page raster pass
  - shader dispatch through `ShadowInstanceMetadata`
  - renderer family-selection policy for directional shadows
  - correctness-first directional VSM runtime:
    - fixed virtual clipmap density per quality tier
    - fixed physical atlas capacity per quality tier
    - no active frame-budget density capping or budget-driven backend fallback
  - resident-page identity now uses a snapped light-space lattice key, backed
    by a bounded physical atlas pool
  - clean resident-page promotion and deterministic eviction now operate on the
    same canonical resident-page key used by selection and residency tracking
  - page-aligned directional clipmap motion reuses overlapping resident pages
    instead of rerasterizing the entire requested working set
  - request feedback now carries absolute resident-page keys, not local page
    indices tied to clip origin
  - spatial dirty-page invalidation marks only overlapping resident pages dirty
    for moved casters, with conservative whole-product fallback when bound
    pairing is not possible
  - directional lighting now uses the same footprint-selected clip family as
    request generation instead of always starting from the finest containing
    clip
  - feedback-driven fine-page refinement now stays sparse per page with bounded
    local dilation instead of clip-wide rectangular inflation
  - shader-side fallback to coarser valid clip levels when a finer virtual page
    is invalid
  - page-interior clamped comparison filtering so atlas neighbors do not bleed
    into the current validation slice
  - `VirtualShadowRequestPass`, which now scans the main depth buffer after
    `DepthPrePass`, writes a deduplicated GPU request-bit mask, and submits
    bounded-lifetime request feedback to `VirtualShadowMapBackend` through a
    slot-safe readback seam
  - CPU visible-receiver request planning now marks per-receiver sparse regions
    as the current-frame bootstrap path instead of merging the whole receiver
    set into one dense clip region
  - coarse-backbone-first mapping order so valid coarser fallback coverage is
    present by construction
- Cache/invalidation realignment, March 10, 2026:
  - canonical resident-page keys now drive request selection, `MarkRendered`,
    eviction, and raster-job tracking
  - address-space compatibility ignores only light-space Z pull-back padding;
    snapped XY light-view translation participates so a clip-lattice jump
    invalidates stale feedback/resident mappings instead of silently reusing
    them
  - content reuse still tolerates page-aligned XY clip-origin motion, but it
    rerasterizes when the light-space Z basis or effective depth-bias mapping
    changes
  - directional feedback compatibility now tracks the clipmap lattice instead
    of only frame age and page-table layout
  - invalidation now dirties overlapping resident pages from changed caster
    bounds instead of flushing the whole product in the common movement case
- Remaining runtime correction scope, March 10, 2026:
  - directional shading must use the same footprint-selected clip policy as
    feedback generation instead of always starting from the finest containing
    clip
  - fine feedback consumption must stay sparse; one clip-wide bounding box per
    clip is not acceptable for complex scenes
  - renderer mid-frame binding updates must not republish virtual shadow
    resources after `VirtualShadowPageRasterPass` has already executed; the
    clustered-lighting update path must preserve the active shadow binding and
    only refresh lighting routing
- Validation evidence:
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
  - Result: shader target built successfully
  - Result: `22` passing VSM-focused tests
  - Result: `34` passing tests in the full `LightManager` functional suite
- Remaining gap:
  - visual validation
  - confirm camera-rotation stability and moving-caster behavior in
    `RenderScene`/Sponza
  - final GPU residency-resolve/update after the new depth/feedback-driven
    request producer
  - invalidation/debug tooling hardening
  - static/dynamic cache separation for large scenes; current invalidation is
    spatial for movable bounds but still monolithic with respect to mixed
    mobility content
  - conservative global invalidation fallback when caster bound count/order
    changes and pages cannot be paired spatially
- default-scene viability for large content; until the final sparse
  residency path exists, the current validation slice must remain opt-in in
  demos and runtime policy

Update, March 12, 2026 directional VSM performance review and recovery plan:

- functional validation is no longer the blocker; performance is
- authoritative performance plan:
  `src/Oxygen/Renderer/Docs/directional_vsm_performance_plan.md`
- summary:
  - dominant cost is still brute-force virtual page raster replay
  - supporting costs are backend page overproduction and full-buffer
    request/resolve overhead
  - Step 1 baseline capture is complete for the active scripted
    moving-camera `RenderScene` benchmark scene
  - Step 2 page-local raster culling is complete with measured reductions in
    steady-state rastered pages (`740.95 -> 420.75`) and shadow draw
    submissions (`6668.55 -> 1465.80`) on the superseded static-camera
    benchmark
- Step 3 page-production tightening / budgeting is now `in_progress`; the
  active moving-camera benchmark now has one measured runtime win from
  capping cold/mismatch bootstrap to the nearest fine clips
  (`120144 ms -> 64755 ms` wall time); the newest coarse-first stress-path
  slice keeps current coarse fallback ahead of fine pages under incompatible
  pressure at near-neutral benchmark cost (`64755 ms -> 62911 ms`); the later
  capacity-fit coarse safety clip and persistent last-coherent publish
  fallback work improved the motion-time publication path, and the newest
  fallback-recovery slice now suppresses dense unpublished fine bootstrap
  while `publish_fallback` is active. On the locked moving-camera benchmark
  that reduced wall time `66602 ms -> 15773 ms`, scheduled pages
  `510.35 -> 233.29`, resolved pages `1427.18 -> 295.12`, and shadow draws
  `2086.53 -> 504.82`, while hot fallback frames dropped from
  `selected=12300, receiver_bootstrap=12288` to
  `selected=12, receiver_bootstrap=0`. The newest publish-compatible
  stale-fallback gate now uses the actual previously published coarse coverage
  with bounded continuous receiver overrun instead of the rejected full-page
  overshoot relaxation; focused VSM tests are green at `48/48`, and the locked
  moving-camera benchmark stayed effectively flat (`15773 ms -> 16156 ms`).
  However, user live validation is still the remaining exit delta for the
  zoom/aggressive-motion wrong-page flashing fix. Step 3 remains open for both
  the remaining accepted-feedback refinement / current-frame reinforcement
  budgeting on publishable frames and that final visual revalidation; see
  `src/Oxygen/Renderer/Docs/directional_vsm_performance_plan.md`
  - frozen recovery order remains baseline capture, page-local raster culling,
    page production tightening, readback reduction, dynamic cache
    specialization, and before/after validation

Update, March 12, 2026 architecture review:

- the current directional VSM backend is frozen for redesign analysis
- authoritative review:
  `src/Oxygen/Renderer/Docs/directional_vsm_architecture_review.md`
- replacement redesign plan:
  `src/Oxygen/Renderer/Docs/directional_vsm_redesign_plan.md`
- the review concludes that the remaining motion-time failures come from the
  authoritative contract itself:
  - continuity is publication-snapshot driven instead of page-table / clipmap
    cache driven
  - coarse fallback is backend-policy driven instead of sample-contract driven

## 14. References

- Microsoft, *Cascaded Shadow Maps*:
  <https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps>
- Microsoft, *Common Techniques to Improve Shadow Depth Maps*:
  <https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps>
- GPU Gems 3, Chapter 10, *Parallel-Split Shadow Maps on Programmable GPUs*:
  <https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus>
- NVIDIA, *Cascaded Shadow Maps* sample notes:
  <https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf>
- Epic Games, *Virtual Shadow Maps in Unreal Engine*:
  <https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine>

## Optimization

The short answer is: real engines keep VSM under control by making shadow work
sparse, cached, and visibility-driven. They do not render a full giant shadow
map every frame. They render only the pages that visible pixels actually need,
keep those pages around across frames, aggressively cull geometry that cannot
affect those pages, and separate the “render/update shadow data” cost from the
“sample/filter shadows on screen” cost. That is the core reason VSMs are
practical in shipping engines.

How the cost is bounded

- The virtual shadow map is huge in address space, but only a small subset gets
  physical backing. UE’s public docs describe 16k virtual maps split into
  128x128 pages, allocated from screen- visible need, not from total scene
  extent.
- For directional lights, engines use clipmaps instead of one monolithic map. In
  UE’s docs, the default directional clipmap span is levels 6 through 22,
  roughly from 64 cm out to about 40 km around the camera.
- For local lights, mip chains and cube faces keep resolution proportional to
  what is actually projected on screen.
- The many-lights Chalmers work uses the same general idea: estimate needed
  resolution from visible receivers, project receiver bounds onto cube maps, and
  back only the touched virtual tiles. Their stated goal is bounded memory
  footprint, roughly proportional to the minimum shadow samples actually needed.

What runs on CPU vs GPU

- In practice, the heavy VSM loop is GPU-first. This is an inference from Epic’s
  docs plus the Chalmers papers.
- GPU side: page marking from visible samples/depth, page-mask tests, HZB/page
  culling, Nanite shadow rasterization, and final shadow projection/filtering.
- CPU side: pass setup, scene/light state changes, invalidation bookkeeping,
  mobility decisions, and legacy-style per-light draw submission for non-Nanite
  content.
- That split matters because UE explicitly shows RenderVirtualShadowMaps(Nanite)
  as a batched path, while non-Nanite rendering still behaves more like classic
  per-light shadow rendering and is therefore much more expensive.

How engines keep it fast

- Cache pages between frames. This is the biggest win. Smooth camera motion is
  usually cheap; moving lights, moving casters, WPO/PDO, skeletal deformation,
  and bad bounds are what explode cost.
- Split static and dynamic caching. UE’s separate static caching avoids
  redrawing expensive static terrain/buildings when only dynamic actors changed.
- Cull against required pages, not just against light frusta. UE exposes stats
  for page-mask culling, HZB culling, frustum culling, etc. The Chalmers work
  similarly uses clustered receiver bounds plus a hierarchy over batches to
  avoid drawing geometry into unused shadow regions.
- Batch where possible. UE’s one-pass projection batches many local lights into
  a clustered shading path instead of paying a separate filtered projection pass
  per small light.
- Keep memory bounded with a physical page pool. UE exposes this directly via
  r.Shadow.Virtual.MaxPhysicalPages; overflow causes corruption/artifacts.

The two independent performance buckets

- Shadow Depths: cost to update/render shadow pages. This is dominated by page
  count, invalidations, and how much geometry is drawn into those pages.
- Shadow Projection: cost to sample/filter shadows during lighting. UE states
  this depends on total shadow samples across the screen, not on page count or
  cache state.
- This split is crucial because different knobs fix different problems. If depth
  is bad, reduce page creation/update. If projection is bad, reduce
  lights-per-pixel and filtering work.

The knobs that matter in real production

- Resolution pressure: r.Shadow.Virtual.ResolutionLodBiasDirectional
  r.Shadow.Virtual.ResolutionLodBiasLocal
  r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving
  r.Shadow.Virtual.ResolutionLodBiasLocalMoving
- Cache/invalidation pressure: avoid moving shadow-casting lights, limit
  WPO/PDO, keep bounds tight, use static/dynamic separation, switch distant
  foliage/material LODs away from deformation.
- Non-Nanite pressure: r.Shadow.RadiusThreshold far-distance fallback to
  Distance Field Shadows for non-Nanite disable distant shadow casting or rely
  on contact shadows where acceptable
- Coarse-page pressure: r.Shadow.Virtual.MarkCoarsePagesLocal
  r.Shadow.Virtual.MarkCoarsePagesDirectional r.Shadow.Virtual.FirstCoarseLevel
  r.Shadow.Virtual.LastCoarseLevel
  r.Shadow.Virtual.NonNanite.IncludeInCoarsePages 0
- Many-light projection pressure: r.UseClusteredDeferredShading 1
  r.Shadow.Virtual.OnePassProjection 1
  r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel
- Soft-shadow sampling pressure: reduce light Source Radius / Source Angle first
  then tune r.Shadow.Virtual.SMRT.SamplesPerRayLocal and
  r.Shadow.Virtual.SMRT.SamplesPerRayDirectional
- Memory pressure: r.Shadow.Virtual.MaxPhysicalPages

What teams actually do

- Convert as much shadow-casting geometry as possible to the engine’s efficient
  path (Nanite in UE).
- Treat deformation as a budgeted feature, not a default.
- Keep important big casters static and stable.
- Swap distant nonessential shadows to cheaper methods.
- Be very careful with many overlapping large local lights.
- Profile with VSM-specific stats and visualizations, not just total GPU time.

So the real production answer is not “VSM is fast because it is virtual.” It is
fast only when the engine enforces four disciplines at once: sparse allocation,
cache reuse, aggressive culling, and separate tuning of shadow-update cost
versus shadow-sampling cost.

Sources:

- Epic UE 5.7 docs: <https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine>

- Chalmers I3D paper: <https://www.cse.chalmers.se/~uffe/ClusteredWithShadows.pdf>
- Chalmers SIGGRAPH talk notes:
  <https://www.cse.chalmers.se/~uffe/ClusteredWithShadowsSiggraphTalk2014.pdf>
- Chalmers TVCG paper:
  <https://www.cse.chalmers.se/~d00sint/more_efficient/clustered_shadows_tvcg.pdf>
- Chalmers thesis: <https://publications.lib.chalmers.se/records/fulltext/192172/192172.pdf>

Update, March 11, 2026 atlas-state debug correction:

- the `RenderScene` atlas inspector no longer infers page activity only from
  physical depth contents
- `VirtualShadowMapBackend` now exports one debug state per physical atlas tile:
  `cleared`, `reused`, or `rewritten`
- `VirtualShadowAtlasDebugPass` uploads that tile-state array through a
  structured-buffer SRV and `VirtualShadowAtlasDebug.hlsl` colors physical page
  borders from the actual current-frame backend classification
- functional evidence:
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:4 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /p:UseMultiToolTask=false /p:CL_MPCount=1 /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 4 --fps 3 --directional-shadows virtual-only`

Update, March 11, 2026 atlas-inspector packing correction:

- `VirtualShadowAtlasDebugPass` had a C++/HLSL constant-buffer layout mismatch:
  three `uint` bindless indices were followed immediately by `uint2
  atlas_dimensions` on the C++ side, but HLSL forced `atlas_dimensions` onto the
  next 16-byte register
- that mismatch corrupted the shader's view of atlas dimensions/page size and
  could collapse the displayed atlas into a thin band
- the pass constants now include explicit padding on both sides of the
  interface, plus compile-time C++ offset assertions
- validation evidence:
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 4 --fps 3 --directional-shadows virtual-only`

Update, March 11, 2026 atlas-inspector legend correction:

- the original `rewritten` border color read as red/orange against the atlas
  fill, and the original `cleared` border color was too muted to read reliably
- the current legend is:
  - `cleared`: blue border
  - `cached but not requested this frame`: gray border
  - `reused this frame`: green border
  - `rewritten this frame`: yellow border
- validation evidence:
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 4 --fps 3 --directional-shadows virtual-only`
  - result: shader target built successfully
  - result: `26/26` VSM-focused LightManager tests passed
  - result: `38/38` LightManager tests passed
  - result: `RenderScene` rebuilt and the short virtual-only smoke run exited
    with code `0`
- remaining gap:
  - live visual confirmation that the atlas border colors match the observed
    churn during camera movement
  - the underlying cache churn bug is still `in_progress`; this change improves
    observability, not the cache policy itself

Update, March 11, 2026 runtime churn diagnosis:

- low-fps `RenderScene` logs showed that the atlas-inspector semantics were not
  the only issue
- the current runtime churn has two upstream causes:
  - directional address-space comparison was still zeroing XY light-view
    translation, so a snapped light  -   Feedback age thresholding prevents stale pages from accumulating.
  -   Current frame request overlap uses heuristics instead of full raycasts to ensure responsiveness under rapid motion.
  -   **Caster Culling:** Pages requested by viewport receivers are bounds-tested against known 2D shadow caster footprints in light space. Any requested page that does not intersect a caster is discarded prior to allocation. The unmapped page gracefully defaults to fully lit upon sampling, drastically reducing GPU page/bandwidth churn for empty regions.

-   **Caching Matrix:** The overall decision matrix takes into account the `frame_sequence`, `address_space_hash` (grid orientation and scale), `candidate_hash` (directional light matrix changes), and `caster_hash`/`content_hash` (dynamic object motion).eep
    `address_space_compatible=true` even after the absolute virtual page
    lattice had shifted
  - the coarse fallback backbone was still seeded from the full camera frustum
    depth span, which made a tiny two-cube scene request thousands of coarse
    pages simply because the camera far plane was large
- the consequence is exactly the observed pattern:
  - stable frames show the atlas fully green because the resident set is being
    re-requested by an oversized coarse backbone
  - motion frames can transiently accept stale feedback keys against the new
    `clip_grid_origin`, reject them as out-of-bounds, and skip both fine-page
    selection and the fallback `receiver_bootstrap` path
- status remains `in_progress` until both runtime causes are corrected and
  revalidated with VSM tests plus another short `RenderScene` log capture

Update, March 11, 2026 runtime motion-stability correction:

- the short low-fps `RenderScene` run against `/.cooked/Scenes/VsmTwoCubes`
  showed that the simple scene now settles correctly in the static case, but
  that result was not enough to close motion stability
- current accepted feedback is still delayed by the request-readback loop, and
  the backend currently shuts off current-frame fine receiver seeding entirely
  once that delayed feedback is accepted
- that policy is good for reducing dense rebootstrap churn, but it is too
  aggressive for motion because the fine-page set can become temporally stale
  while the camera is moving
- the required fix is not a return to the old full receiver bootstrap; it is a
  bounded hybrid:
  - accepted feedback stays the primary sparse fine-page signal
  - current-frame receivers add only a tightly clamped reinforcement band for
    the nearest fine clips so current shading does not outrun delayed feedback
- evidence:
  - `out/build-vs/vsm-two-cubes-runtime-20260311.txt`
  - static settle point shows `request=accepted`, `selected=601`,
    `feedback_seed=115`, `feedback_refine=553`, `receiver_bootstrap=0`,
    `mapped_pages=601`, `resident_pages=1536`
- status remains `in_progress` until the hybrid fine-request path is
  implemented and validated with focused tests plus another low-fps runtime
  capture

Update, March 11, 2026 accepted-feedback reinforcement implementation:

- `VirtualShadowMapBackend` now keeps a bounded current-frame receiver
  reinforcement path even when delayed request feedback is accepted
- the reinforcement is intentionally narrow:
  - only the nearest fine clips participate
  - receiver extents are clamped to a small page-space band
  - this avoids falling back to the old dense receiver bootstrap
- diagnostics now separate:
  - `receiver_bootstrap`: cold/current fallback seeding when feedback is not in
    use
  - `receiver_reinforce`: bounded current-frame reinforcement while feedback is
    accepted
- evidence:
  - focused regression:
    `ShadowManagerPublishForView_VirtualAcceptedFeedbackUsesBoundedReceiverReinforcementDuringClipShift`
  - `out/build-vs/vsm-two-cubes-runtime-20260311-hybrid.txt`
  - steady accepted-feedback frames now show:
    - `request=accepted`
    - `selected=652`
    - `receiver_bootstrap=0`
    - `receiver_reinforce=51`
    - `rerasterized=0`
- this is a real runtime improvement over the previous accepted-feedback state
  (`receiver_bootstrap=0` and no current-frame fine reinforcement at all), but
  overall status remains `in_progress` until live camera-motion validation
  confirms that visible shadow instability is gone rather than just reduced

Update, March 11, 2026 reinforcement strategy correction:

- live motion validation showed that receiver-center reinforcement is still the
  wrong proxy for current-frame visible demand
- on large receivers such as the floor, reinforcing around object/bound centers
  can keep pages alive on visible caster surfaces while still missing the
  actual shadow footprint on the floor
- that failure mode matches the reported artifacts: partial shadows and shadows
  appearing on top faces of casters
- the corrected next step is to replace that object-center reinforcement with a
  bounded current-frame frustum reinforcement on the nearest fine clips
- status remains `in_progress` until that frustum-based reinforcement is
  implemented and revalidated

Update, March 11, 2026 frustum-reinforcement regression:

- the first frustum-based reinforcement implementation over-expanded fine-page
  demand and is not shippable in its current form
- evidence from `out/build-vs/vsm-two-cubes-runtime-20260311-frustum.txt`:
  - accepted-feedback frames reached `selected=9770`
  - `current_reinforce=9122`
  - `allocated=1531`
  - `evicted=1531`
  - `alloc_failures=8234`
- that behavior is a regression because the two-cube scene has only two small
  casters and a tiny on-screen receiver footprint; whole-frustum fine
  reinforcement is clearly selecting far more pages than current shading needs
- the corrected next step is narrower: keep accepted feedback as the primary
  sparse signal and reinforce only the newly exposed fine-page delta bands
  between the previous and current frustum regions
- status remains `in_progress` until the frustum-delta reinforcement path is
  implemented and validated

Update, March 11, 2026 absolute frustum-delta reinforcement:

- the accepted-feedback reinforcement path now stores per-clip frustum regions
  in absolute resident-page space and reinforces only the newly exposed delta
  bands between the previous and current view regions
- this avoids the old object-center proxy and the later whole-frustum
  fine-request blowup
- the delta path is capped per clip so a bad comparison cannot silently
  reintroduce multi-thousand-page fine selection
- evidence:
  - focused tests:
    `ShadowManagerPublishForView_VirtualAcceptedFeedbackUsesBoundedDeltaReinforcementDuringClipShift`
  - `out/build-vs/vsm-two-cubes-runtime-20260311-delta.txt`
  - accepted-feedback frames now show:
    - `selected=778`
    - `feedback_seed=108`
    - `feedback_refine=561`
    - `receiver_bootstrap=0`
    - `current_reinforce=169`
    - `alloc_failures=0`
    - warm frames reusing the whole selected set with `rerasterized=0`
- this is a real improvement over the previous frustum-reinforcement regression
  (`selected=9770`, `current_reinforce=9122`, `alloc_failures=8234`), but
  status remains `in_progress` until live camera-motion validation confirms the
  visible shadow instability is gone rather than just the allocator churn

Update, March 11, 2026 accepted-feedback delta baseline correction:

- motion validation showed that the current delta-reinforcement baseline is
  still wrong under real camera movement
- the backend was comparing the current frustum region against the immediately
  previous publication, but the accepted page seed was still coming from the
  older request-feedback source frame
- that means the reinforcement band only covered the most recent frame-to-frame
  motion while the reused page set could still be several frames older
- this directly explains motion artifacts after the cache/request optimization:
  the cache is reusing an older page set, but the current-frame patch only
  repairs the last-frame delta instead of the full feedback-age delta
- the corrected next step is to store the feedback source-frame absolute
  frustum regions alongside the accepted feedback and compute current
  reinforcement against that source region, not against the previous
  publication
- status remains `in_progress` until that baseline fix is implemented and
  validated

Update, March 11, 2026 directional content-reuse validity:

- allocator churn is no longer the only active problem; live motion still
  shows partially rendered or misplaced directional shadows after the cache
  optimization
- the remaining likely fault is the directional clean-page reuse predicate
- the backend currently allows page contents to stay clean across changes in
  the effective directional depth mapping because it compares clip scale but
  ignores the stored depth-bias term
- that is unsafe: VSM page ownership may remain valid in XY while the stored
  normalized depth values are no longer valid for current shading
- this directly matches the observed artifact pattern: pages are reused, but
  the resulting shadows are partial, displaced, or appear on caster top faces
- the corrected next step is to separate the two reuse decisions cleanly:
  address-space compatibility must invalidate on snapped XY lattice shifts but
  can ignore pure Z pull-back changes, while clean-page reuse must rerasterize
  whenever the directional depth mapping changes
- status remains `in_progress` until that correction is implemented and
  validated

Update, March 11, 2026 directional depth-basis reuse fix:

- the backend now treats directional page-address reuse and directional
  page-content reuse as two different questions
- address-space compatibility now preserves XY light-view translation so a
  snapped clip-lattice shift invalidates stale feedback/resident mappings on
  the snap frame; it still zeroes Z pull-back padding so reuse is allowed for
  pure along-light pull-back changes
- clean-page reuse is now stricter:
  - the comparable light-view keeps the Z translation component but zeros XY so
    page contents remain reusable across page-aligned XY relayout when the
    stored depth basis is still valid
  - the comparable clip metadata keeps the shader-visible depth-mapping terms
    used during VSM sampling
- this prevents a cached page from staying clean when the current directional
  light-space depth basis differs from the basis used to rasterize that page
- focused integration coverage:
  `ShadowManagerPublishForView_VirtualInvalidatesCleanPagesWhenDepthMappingChanges`
- validation:
  - `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
    passed `31/31`
  - `Oxygen.Renderer.LightManager.Tests.exe` passed `43/43`
  - low-fps runtime smoke run:
    `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 12 --fps 3 --directional-shadows virtual-only`
    completed successfully with log capture in
    `out/build-vs/vsm-runtime-20260311-depth-reuse-gate.txt`
- overall status remains `in_progress` until the live interactive motion
  artifacts are rechecked in the demo scene

Update, March 11, 2026 directional snap-boundary dropout fix:

- the missing-shadow regression at directional grid snap boundaries was caused
  by the address-space comparable light view incorrectly zeroing XY
  translation
- when the light eye snapped, the clip lattice moved in absolute page space,
  but the compatibility gate falsely treated the old and new address spaces as
  identical
- that let delayed feedback survive into the wrong lattice:
  - stale resident-page keys were decoded against the new `clip_grid_origin`
  - the keys were rejected as out-of-bounds
  - both sparse fine selection and the fallback `receiver_bootstrap` seed were
    skipped for that frame
- the comparable address-space light view now preserves XY translation and
  zeros only Z pull-back padding, so snapped XY lattice shifts invalidate the
  cache exactly on the snap frame while still allowing reuse when only the
  along-light pull-back changes
- spatial dirty-page invalidation now projects previous caster bounds through
  the previous frame `light_view`, so resident keys generated for the old
  bound line up with the lattice that actually owns those pages
- focused non-regression coverage now exists for this fault:
  - `LightManagerTest.ShadowManagerPublishForView_VirtualFeedbackAddressSpaceTracksSnappedXYTranslationButIgnoresZPullback`
    proves snapped XY light-view translation changes directional feedback
    address-space identity while pure Z pull-back does not
  - `LightManagerTest.ShadowManagerPublishForView_VirtualIncompatibleFeedbackRebootsReceiverBootstrap`
    proves incompatible feedback is rejected and re-enters
    `receiver_bootstrap` instead of silently skipping both refinement and
    fallback seeding
- follow-up page-table publication work is now in code:
  - the shader-visible page table uses a persistent per-view GPU buffer with a
    stable bindless slot across republishes
  - valid current-frame page-table entries now carry the documented
    `requested this frame` bit
- overall status remains `in_progress` in this document:
  - the code change is present in the last commit
  - broader directional VSM work is still unfinished
  - validation evidence for this exact regression is now:
    - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualFeedbackAddressSpaceTracksSnappedXYTranslationButIgnoresZPullback`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualIncompatibleFeedbackRebootsReceiverBootstrap`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualPageTablePublicationUsesStablePerViewGpuBuffer`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualPageTableMarksMappedPagesRequestedThisFrame`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleDoesNotSuppressCpuPendingJobsYet`
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPublishForView_VirtualResolvedScheduleRejectsIncompatibleAddressSpace`
    - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
    - `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 40 --fps 100 --directional-shadows virtual-only`
  - the live `RenderScene` capture proved the resolve pass was active and
    producing compact schedules (`scheduled_pages=91` after feedback
    stabilized), but that capture still settled into clean-page reuse before a
    live reraster frame exercised the prune path
