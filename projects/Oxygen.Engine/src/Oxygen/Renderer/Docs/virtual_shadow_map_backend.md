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
- page size is now capped by both quality tier and atlas budget, so sparse
  page grids can retain more authored directional resolution without allowing
  the physical atlas to grow without bound
- ultra quality now grants a larger physical atlas budget at 16 ms frame
  budgets so fine virtual pages can carry more texels before residency becomes
  the limiting factor
- the live implementation now also decouples virtual address-space density from
  physical residency capacity:
  - higher tiers use more virtual clip levels and denser page grids than the
    initial validation slice
  - the physical atlas is budget-bounded and no longer sized as if every
    virtual page had to be resident simultaneously
  - when virtual clip count exceeds authored conventional cascade count, the
    extra clip boundaries are generated from the practical split policy instead
    of indexing past authored cascade distances

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
- a residency resolve pass deduplicates requests and maps them to physical
  pages
- dirty/newly allocated pages are rasterized instead of the whole fixed
  working set

Important consequence:

- Unlike the current conventional directional path, virtual directional page
  rendering is scheduled after main depth pre-pass so the later sparse
  receiver-driven request path can land without moving the backend again.

That is acceptable because the shadow architecture is already backend-aware and
does not require conventional and virtual families to share identical pass
placement.

## 7. Request Generation

### 7.1 Receiver-driven requests

The final sparse VSM path is receiver-driven. The current implementation now
starts from visible receiver demand on the CPU, but it is still an intermediate
slice rather than the final depth/feedback-driven sparse request path.

Current intermediate behavior:

- `VirtualShadowMapBackend` computes one camera-relative clipmap grid per level
- visible receiver bounds from `PreparedSceneFrame` are projected into that
  grid to request pages per clip level
- explicit one-shot per-view request feedback can override that CPU request
  source when the renderer provides compatible clip/page coordinates
- requested pages are prioritized by clip level, receiver overlap, and
  proximity to the camera-relative clip center under the current page budget
- the shader-visible page table is populated only for the selected resident
  pages
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

- live residency resolve/update still happens on the CPU after feedback
  readback; there is not yet a GPU-side residency resolve/update pass
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
4. choose the finest clip level covering the point
5. compute `(page_x, page_y)` for that level
6. mark or append a request

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
- on safe slot reuse, the CPU decodes that mask and submits one-shot request
  feedback to `VirtualShadowMapBackend`
- until the first safe feedback arrives for a view, or if no compatible
  feedback is available, the backend falls back to the existing CPU
  visible-receiver request path

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

Per-frame update budgets are also tier-owned:

- max newly allocated pages
- max dirty-page rerenders
- max total page renders

The first implementation should keep these budgets explicit in
`RendererConfig`.

Current runtime note:

- the current validation slice is bounded and partially sparse, but not yet the
  final feedback-driven sparse VSM path
- it already consumes the engine-derived GPU frame budget
  (`FrameContext::BudgetStats.gpu_budget`) to cap clipmap density
- in `prefer-virtual`, the backend may deterministically decline the virtual
  path and fall back to conventional directional shadows when the estimated
  virtual work exceeds the current frame GPU budget
- page requests are currently generated from
  runtime depth feedback when available; otherwise they fall back to
  `PreparedSceneFrame.visible_receiver_bounding_spheres`, are prioritized per
  clip, and are clamped by the current frame budget
- `VirtualShadowRequestPass` is now the live runtime producer for that feedback
  path; it runs after `DepthPrePass`, scans the main depth buffer, and submits
  one-shot request feedback through a slot-safe readback path
- a deterministic centered resident window is only used as fallback when the
  receiver-driven selection requests no pages
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
- virtual page ownership must stay on the un-biased receiver position, while
  local in-page sampling and depth comparison use the biased receiver position
  so normal-offset bias can move the sample footprint without destabilizing
  virtual addressing
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

The correct implementation order is:

1. add `VirtualShadowMapBackend` resource ownership
2. add directional virtual metadata and `ShadowFrameBindings` slots
3. add request generation pass
4. add residency resolve / allocation pass
5. add virtual page raster jobs and page rendering pass
6. add shader sampling path for directional virtual products
7. add debug and automated validation

Only after the above is stable:

- add family selection policy to actively choose virtual vs conventional for
  directional products
- then expand to spot/point virtual products

### 13.1 Next Milestone

The next implementation milestone is:

`Directional VSM Production Candidate`

This milestone is complete only when:

- depth/feedback-driven request generation is live
- request deduplication and budgeted page scheduling are live
- residency reuse/invalidation/eviction are deterministic and content-safe
- `virtual-only` is stable under camera rotation/translation in validation
  scenes including Sponza
- directional VSM edge quality is no longer visibly below the conventional
  directional path in normal validation scenes
- the backend exposes the required debug surfaces for request, residency,
  fallback, and budget diagnosis

This is the correct next bar before any expansion to local-light virtual
shadows.

Implementation status, March 9, 2026:

- `in_progress`
- The current runnable slice is now in code:
  - backend resource ownership
  - directional virtual metadata publication
  - virtual page raster pass
  - shader dispatch through `ShadowInstanceMetadata`
  - renderer family-selection policy for directional shadows
  - frame-budget-aware clipmap density capping and deterministic
    `prefer-virtual` fallback to conventional when the current GPU frame budget
    cannot afford the requested virtual work
  - budget-bounded CPU-visible-receiver-driven page requests
  - deterministic centered resident-window fallback when receiver-driven page
    selection requests nothing
  - cross-frame resident physical-page reuse and partial rerasterization for
    snapped-identical plans
  - content-safe invalidation for resident-page reuse when light or caster
    inputs change under otherwise identical snapped virtual plans
  - shader-side fallback to coarser valid clip levels when a finer virtual page
    is invalid
  - page-interior clamped comparison filtering so atlas neighbors do not bleed
    into the current validation slice
  - `VirtualShadowRequestPass`, which now scans the main depth buffer after
    `DepthPrePass`, writes a deduplicated GPU request-bit mask, and submits
    one-shot request feedback to `VirtualShadowMapBackend` through a slot-safe
    readback seam
  - CPU visible-receiver request planning as fallback when compatible request
    feedback is not yet available for a view
- Validation evidence:
  - `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Engine/oxygen-engine.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- Remaining gap:
  - visual validation
  - final GPU residency-resolve/update after the new depth/feedback-driven
    request producer
  - dirty-page tracking and eviction
  - invalidation/debug tooling hardening
  - default-scene viability for large content; until the final sparse
    residency path exists, the current validation slice must remain opt-in in
    demos and runtime policy

## 14. References

- Microsoft, *Cascaded Shadow Maps*:
  https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps
- Microsoft, *Common Techniques to Improve Shadow Depth Maps*:
  https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
- GPU Gems 3, Chapter 10, *Parallel-Split Shadow Maps on Programmable GPUs*:
  https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
- NVIDIA, *Cascaded Shadow Maps* sample notes:
  https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf
- Epic Games, *Virtual Shadow Maps in Unreal Engine*:
  https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine
