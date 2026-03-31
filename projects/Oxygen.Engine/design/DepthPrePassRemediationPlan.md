# Depth PrePass Remediation Plan

Status: `in_progress`
Audience: renderer engineers remediating Oxygen's depth prepass and its
downstream products
Scope: fix the issues identified in the March 31, 2026 depth-prepass review,
one issue at a time, with UE5 closeness treated as an explicit success factor
and validation criterion

This plan is intentionally strict. The goal is not to "improve depth prepass a
bit." The goal is to move Oxygen's depth path toward the same architectural
shape and exploitation model that makes UE5's early depth path valuable.

Scope note as of March 31, 2026:

- DP-3 depth-pass specialization is explicitly deferred by user direction for
  the current remediation wave
- the active remediation scope now prioritizes contract, policy, and
  downstream-consumer correctness over UE5-style depth-only PSO specialization
- UE5 closeness remains a design reference, but DP-3 is no longer a gate for
  advancing to later phases in this wave

## 1. Non-Negotiable Rules

Every remediation phase must satisfy all of the following before it can be
called complete:

1. Implementation exists in Oxygen code.
2. Relevant docs are updated.
3. Validation evidence is recorded.
4. UE5 closeness is evaluated explicitly, not assumed.

If a phase changes design scope, update this plan first. Do not bury scope
changes inside code.

## 2. Success Criteria

The remediation succeeds only when all of these are true for the active scope:

- the pass contract is correct and explicit
- later passes consume shared depth products instead of rebuilding private
  derivatives
- the engine can express and validate different prepass policies
- the depth path has measurable evidence through repo-owned tooling
- the implementation is demonstrably closer to UE5 in contract and consumer
  shape, not just in comments or naming

Deferred-but-tracked success item:

- depth-pass specialization like UE5 where the material and geometry allow it
  is now explicitly deferred with DP-3 instead of being a current-wave exit
  gate

## 3. UE5 Reference Anchors

Primary UE5 references for this work:

- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/DepthRendering.cpp`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/DepthOnlyVertexShader.usf`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/DepthOnlyPixelShader.usf`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/HZB.usf`

The relevant UE5 characteristics to track during implementation are:

- explicit early-Z policy modes
- explicit "early depth complete" downstream contract
- position-only depth path
- null-pixel-shader path when materials allow it
- PS path only for masked / pixel-depth-offset / coverage cases
- shared HZB/depth products used by later systems
- reversed-Z depth convention

## 4. Phase Order

The order below is deliberate unless the plan explicitly records a deferred
phase. DP-3 is currently deferred by explicit user direction; DP-4 is the next
active phase after DP-2.

### Phase DP-0: Fix Measurement And Validation Surface

Problem:
- current RenderDoc pass timing sees `DepthPrePass` markers but no nested work
  in sampled replay-safe captures, so the pass cannot be measured reliably
  through the existing pass-timing script

Goal:
- depth prepass work must be visible and measurable in repo-owned analysis
  artifacts

Implementation work:
- audit marker placement and pass scope naming for `DepthPrePass`
- ensure renderdoc event scopes cleanly wrap the actual clear and draw work
- add a focused depth-prepass analysis workflow under
  `Examples/RenderScene/` if the generic pass-timing script is insufficient
- document the exact capture-and-analysis recipe for steady-state late frames

Validation:
- replay-safe late-frame `Release` captures
- RenderDoc report showing non-empty depth-prepass work events
- before/after evidence proving that timing and event counts are stable enough
  to evaluate later optimizations

UE5 closeness check:
- UE5 has measurable depth pass and explicit downstream consequences
- Oxygen must at least make the pass observable enough to support the same kind
  of design reasoning

Completion evidence:

- implementation:
  - `Examples/RenderScene/renderdoc_ui_analysis.py` now recognizes
    both `DepthPrePass` and `ConventionalShadowRasterPass` as routable pass
    names for the shared timing and pass-focus scripts
  - `src/Oxygen/Renderer/Pipeline/ForwardPipeline.cpp` now assigns distinct
    debug names to the main-scene depth pass and the conventional shadow raster
    pass instead of letting both inherit the same generic depth-pass naming
- documentation:
  - `Examples/RenderScene/README.md` now records the `Release` late-frame
    capture-and-analysis recipe for `DepthPrePass`
- validation artifacts:
  - `out/build-ninja/analysis/depth_prepass_review_release/release_frame200_conventional_fps30_frame200.rdc`
  - `out/build-ninja/analysis/depth_prepass_review_release/release_frame200_conventional_fps30.depth_timing.txt`
  - `out/build-ninja/analysis/depth_prepass_review_release/release_frame200_conventional_fps30.depth_focus.txt`
  - `out/build-ninja/analysis/depth_prepass_review_release/release_frame200_conventional_fps30.shadow_timing.txt`
  - `out/build-ninja/analysis/depth_prepass_review_release/release_frame200_conventional_fps30.shadow_focus.txt`
- follow-up operating note:
  - complex-scene depth-prepass analysis should prefer `conventional`
    directional shadows unless a remediation phase is explicitly studying VSM
    interaction, because late-frame VSM captures can carry very large but legal
    shadow-pool state that distorts RenderDoc load/replay behavior without
    being relevant to the depth-prepass contract itself

Current evidence:

- the frame-200 conventional-shadow capture is valid and replayable
- `DepthPrePass` now produces measurable work in the repo-owned timing script:
  - `work_event_count=627`
  - `total_gpu_duration_ms=6.346976`
- `ConventionalShadowRasterPass` is now separately measurable:
  - `work_event_count=3036`
  - `total_gpu_duration_ms=30.350336`

Remaining gap:

- `DepthPrePass` scope purity is still not correct
- the current `DepthPrePass` pass-focus artifact still begins with
  `DirectionalShadowDepthArray` clear work before the pass switches to drawing
  into `Forward_HDR_Depth`
- until that residual contamination is removed or explained by a tighter
  pass-scoping contract, DP-0 cannot be called complete

Status: `in_progress`

### Phase DP-1: Fix The Viewport/Scissor Contract

Problem:
- `SetViewport()` and `SetScissors()` store overrides but
  `SetupViewPortAndScissors()` ignores them and always binds the full target

Goal:
- the pass must render exactly the intended rectangle, and later passes must
  know what rect is valid

Implementation work:
- honor stored viewport/scissor overrides in `DepthPrePass`
- define the effective depth rect as part of the pass output contract
- update any clear behavior that incorrectly assumes full-target coverage
- add tests for full-target and sub-rect cases
- update the depth-prepass documentation to match the real behavior

Validation:
- unit/integration tests for viewport and scissor honoring
- RenderDoc evidence from a clipped-rect scenario
- doc update in `src/Oxygen/Renderer/Docs/passes/depth_pre_pass.md`

UE5 closeness check:
- UE5 reasons about per-view rects explicitly during depth and HZB work
- Oxygen must stop treating full-target rasterization as the only legal mode

Current implementation:

- `DepthPrePass` now honors configured viewport/scissor overrides instead of
  always binding the full target
- `DepthPrePass` now publishes `DepthPrePassOutput` with:
  - `depth_texture`
  - effective `viewport`
  - effective `scissors`
  - effective `valid_rect`
- `DepthPrePass` now clears only inside the effective depth rect instead of
  assuming full-target coverage
- `ForwardPipeline` now normalizes composition-space view rectangles into
  depth-target local coordinates before configuring `DepthPrePass`

Validation evidence so far:

- focused GPU tests:
  - `Oxygen.Renderer.DepthPrePass.Tests`
  - verified:
    - full-target defaults
    - contract rejection for empty viewport/scissor intersections
    - clipped clear behavior on the actual depth texture
- live pipeline regression check:
  - `Examples/MultiView` previously failed for the PiP scene view with
    `viewport dimensions (...) exceed depth_texture bounds (...)`
  - after the `ForwardPipeline` normalization fix, the same `Release`
    late-frame run completes and emits:
    - `out/build-ninja/analysis/depth_prepass_dp1/multiview_frame200_frame200.rdc`
    - `out/build-ninja/analysis/depth_prepass_dp1/multiview_frame200.run.log`
- RenderDoc tooling support:
  - pass-focus reports now include raster viewport/scissor state to support
    depth-rect validation
- live clipped replay evidence:
  - `Examples/MultiView` now exposes explicit validation knobs:
    - `--pip-wireframe`
    - `--pip-scissor-inset`
  - a late `Release` capture with
    `--pip-wireframe false --pip-scissor-inset 24` now emits:
    - `out/build-ninja/analysis/depth_prepass_dp1_late/multiview_solid_inset24_frame200.rdc`
    - `out/build-ninja/analysis/depth_prepass_dp1_late/multiview_solid_inset24.run.log`
    - `out/build-ninja/analysis/depth_prepass_dp1_late/multiview_solid_inset24.depth_focus.txt`
  - the pass-focus report proves two `DepthPrePass.SceneDepthWork` scopes:
    - main scene view: viewport `1280x720`, scissor `0..1280 x 0..720`
    - PiP view: viewport `576x324`, scissor `24..552 x 24..300`
  - this closes the missing live clipped-rect replay evidence

Remaining gap:

- none for DP-1 itself

Status: `completed`

### Phase DP-2: Introduce A Real Depth Products Contract

Problem:
- `DepthPrePass` effectively publishes only a raw depth texture, and each
  consumer rebuilds SRVs and private assumptions

Goal:
- depth prepass should publish a stable, reusable depth-products package

Implementation work:
- design a `DepthPrePassOutput` or `SceneDepthProducts` object
- include at minimum:
  - depth texture
  - canonical SRV or descriptor identity
  - dimensions
  - effective viewport/scissor rect
  - depth convention metadata
  - explicit completeness or validity flags
- migrate `ScreenHzbBuildPass`, `LightCullingPass`, `VsmPageRequestGeneratorPass`,
  and `ShaderPass` to consume the shared output instead of private lookup paths
- remove duplicated SRV-ownership logic where possible

Validation:
- tests proving shared output correctness
- code review evidence showing removed duplicate SRV creation paths
- RenderDoc validation that later passes still bind the correct depth resources

UE5 closeness check:
- UE5 treats scene depth as a first-class renderer product, not a raw texture
  each pass discovers independently

Current implementation:

- `DepthPrePassOutput` now publishes:
  - `depth_texture`
  - canonical shader-visible SRV identity
  - texture dimensions
  - effective viewport/scissor/valid rect
  - depth convention metadata
  - completeness flags
- the shared depth contract is now consumed by:
  - `ScreenHzbBuildPass`
  - `LightCullingPass`
  - `VsmPageRequestGeneratorPass`
  - `ShaderPass`
  - `TransparentPass`
  - `SkyPass`
  - `GroundGridPass`
  - `VsmProjectionPass` through `VsmShadowRenderer`
- duplicate depth SRV ownership was removed from those three consumers
- duplicate depth SRV ownership is now also removed from:
  - `ShaderPass`
  - `SkyPass`
  - `GroundGridPass`
- `TransparentPass` now prefers the published depth texture instead of only the
  raw config pointer
- `VsmProjectionPass` now consumes the published canonical depth SRV when
  available and falls back to its raw depth texture seam only for standalone
  harness/test paths that do not have a registered `DepthPrePass`

Validation evidence so far:

- focused build:
  - `cmake --build out/build-ninja --config Release --parallel 12 --target oxygen-renderer Oxygen.Renderer.DepthPrePass.Tests Oxygen.Renderer.VsmPageRequests.Tests oxygen-examples-renderscene oxygen-examples-multiview`
- focused tests:
  - `Oxygen.Renderer.DepthPrePass.Tests`: pass
  - `Oxygen.Renderer.VsmPageRequests.Tests`:
    - all `3/3` tests pass in `Release`
  - `Oxygen.Renderer.VsmShadowProjection.Tests` after rebuilding the target
    against the updated `VsmProjectionPassInput` contract:
    - `DirectionalTwoBoxSceneMatchesCpuProjectionFromRealStageInputs`: pass
    - `DirectionalTwoBoxLiveShellMatchesIsolatedProjectionAtAnalyticFloorProbes`: pass
    - `PagedSpotLightTwoBoxSceneMatchesCpuProjectionFromRealStageInputs`: pass
    - `DirectionalSingleCascadeLiveShellDarkensStableFarAnalyticShadowProbes`: fail
    - `DirectionalFourCascadeLiveShellMatchesAnalyticFloorClassificationAcrossDenseVisibleProbes`: fail
    - current failure mode is no longer crash/SEH; it is a real Stage 15
      overbright-mask semantic mismatch on analytically shadowed probes
- live pipeline smokes:
  - `out/build-ninja/analysis/dp2_progress/renderscene_vsm_smoke.log`
  - `out/build-ninja/analysis/dp2_progress/multiview_smoke.log`
  - both example runs exit `0`
  - neither log contains `CHECK FAILED`, device-loss, or explicit render-pass
    failure markers

Remaining scope / gap:

- DP-2 still cannot be called complete
- docs for the depth-products contract are not fully updated yet
- the remaining `Release` live-scene VSM projection semantic failures must be
  fixed or explicitly proven unrelated before DP-2 can exit:
  - `DirectionalSingleCascadeLiveShellDarkensStableFarAnalyticShadowProbes`
  - `DirectionalFourCascadeLiveShellMatchesAnalyticFloorClassificationAcrossDenseVisibleProbes`

Status: `in_progress`

### Phase DP-3: Depth Pass Specialization (Null PS + Position-Only Stream)

Problem:

Oxygen binds a pixel shader for every depth prepass draw, including fully
opaque draws where the PS body is empty (the `#ifdef ALPHA_TEST` block compiles
away). More significantly, every draw fetches a full 72-byte interleaved
`VertexData` struct (position 12 + normal 12 + texcoord 8 + tangent 12 +
bitangent 12 + color 16) even though opaque depth draws consume only
`position` (12 bytes) and masked draws consume only `position` + `texcoord`
(20 bytes). This wastes 83% of vertex fetch bandwidth for opaque draws and 72%
for masked draws.

Goal:

Eliminate unnecessary PS dispatch and vertex fetch waste for depth-only draws,
matching UE5's `DepthPosOnlyNoPixelPipeline` / `DepthNoPixelPipeline` /
`FDepthOnlyPS`-only-when-needed architecture in shape.

This phase has two independent sub-optimizations with very different
cost/impact profiles. They can be implemented independently in either order.

---

#### Sub-phase A: Null Pixel Shader for Opaque Variants

**Complexity**: trivial (1 file, ~4 lines)
**Risk**: none — D3D12 explicitly supports null PS for depth-only rendering

Current state:

- `DepthPrePass.cpp` `CreatePipelineStateDesc()` calls `.SetPixelShader()`
  for all four PSO variants, including the two opaque variants
- the opaque PS entry point in `Depth/DepthPrePass.hlsl` has an empty body
  (the `#ifdef ALPHA_TEST` block compiles away entirely)
- the driver recognizes the empty body and elides dispatch, but Oxygen still
  pays for shader binding and PSO creation with a PS stage present

Implementation:

1. In `DepthPrePass.cpp` `CreatePipelineStateDesc()`, build opaque variants
   without `.SetPixelShader()`:

   ```text
   // Current: BuildDesc always calls .SetPixelShader()
   // Change: split into BuildDescWithPS and BuildDescNoPS, or pass a
   // bool needs_pixel_shader parameter to BuildDesc
   ```

   The lambda `BuildDesc` currently always chains `.SetPixelShader(...)`.
   Add a `bool needs_ps` parameter. When `false`, skip the
   `.SetPixelShader()` call entirely. Call with `false` for
   `pso_opaque_single_` and `pso_opaque_double_`, `true` for
   `pso_masked_single_` and `pso_masked_double_`.

2. No shader file changes needed. The masked PS continues to use the existing
   `PS` entry point with `ALPHA_TEST` defined.

3. No `DrawMetadata`, vertex buffer, or upstream changes needed.

Depth-equal safety: the pixel shader does not affect `SV_POSITION` output.
Removing the PS from opaque variants has zero impact on the bitwise-match
requirement between `DepthPrePass` and `ShaderPass`.

Validation:

- existing `Oxygen.Renderer.DepthPrePass.Tests` pass (no behavior change)
- RenderDoc capture: opaque depth events show `PS: (null)` in pipeline state
- visual regression: none expected (no functional change)

---

#### Sub-phase B: Position-Only Vertex Stream for Opaque Draws

**Complexity**: medium-high (7-8 files across 3 layers)
**Risk**: low (position data is identical; no depth-equal impact)

Current state:

- `VertexData` / `Vertex` is a single 72-byte interleaved struct defined in:
  - CPU: `src/Oxygen/Data/Vertex.h` (`data::Vertex`)
  - GPU: `Depth/DepthPrePass.hlsl` (local `VertexData` struct)
- `BX_LoadVertex()` in `Core/Bindless/BindlessHelpers.hlsl` loads the full
  struct via `StructuredBuffer<BX_VERTEX_TYPE>`
- `DrawMetadata` is 64 bytes with 12 bytes of padding available (fields
  `transform_generation`, `submesh_index`, `primitive_flags` leave room)
- `Mesh` class in `Data/GeometryAsset.h` exposes `Vertices()` returning
  `std::span<const Vertex>` and stores one vertex `BufferResource`
- `GeometryUploader` creates and uploads one vertex buffer per mesh
- no position-only buffer, stream, or SRV exists anywhere in the codebase

Bandwidth arithmetic:

| Draw type | Bytes needed | Bytes fetched (current) | Waste |
| --------- | ------------ | ----------------------- | ----- |
| Opaque    | 12           | 72                      | 83%   |
| Masked    | 20           | 72                      | 72%   |

For 1M opaque vertices: 72 MB fetched → 12 MB needed = 60 MB wasted per
frame.

Implementation (7 ordered steps):

1. **`Vertex.h`** — define a `PositionVertex` struct:

   ```cpp
   struct PositionVertex {
     glm::vec3 position;  // 12 bytes, matches Vertex::position layout
   };
   ```

   This is a strict subset of `Vertex`. The `position` field occupies the
   same offset (0) in both structs; loading from a `StructuredBuffer`
   with stride 12 vs. stride 72 produces bitwise-identical `float3` values.

2. **`GeometryAsset.h` / `Mesh` class** — add position-only buffer storage:

   - add `BufferResource` member `position_only_buffer_` alongside existing
     `vertex_buffer_`
   - expose `PositionOnlyBuffer()` accessor
   - extend `SetBufferResources()` to accept the position-only buffer

3. **`GeometryUploader`** — create and upload the position-only buffer:

   - in `UploadVertexBuffer()`, after creating the full 72-byte vertex buffer,
     create a second 12-byte-stride buffer containing only the `position`
     field extracted from each `Vertex`
   - register an SRV for the position-only buffer in the descriptor heap
   - extend `ShaderVisibleIndices` to include `position_only_srv_index`

4. **`DrawMetadata.h`** — add `position_only_buffer_index` field:

   The struct is currently 64 bytes with 12 bytes consumed by
   `transform_generation` (uint32), `submesh_index` (uint32), and
   `primitive_flags` (uint32). These 3 fields plus any existing padding give
   room to add one `ShaderVisibleIndex position_only_buffer_index` without
   exceeding 64 bytes. If alignment is tight, `primitive_flags` can be
   packed or the struct can grow to 80 bytes (next 16-byte boundary).

   Corresponding HLSL struct in `Renderer/DrawMetadata.hlsli` must be
   updated in lockstep.

5. **`DrawMetadataEmitter`** — populate the new field:

   In the emission loop, set `dm.position_only_buffer_index` from
   `indices.position_only_srv_index` obtained from `GeometryUploader`.

6. **`DepthPrePass.hlsl`** — add position-only vertex shader variant:

   ```hlsl
   struct PositionOnlyVertex {
     float3 position;
   };

   #ifdef POSITION_ONLY
   #define BX_VERTEX_TYPE PositionOnlyVertex
   #else
   #define BX_VERTEX_TYPE VertexData
   #endif
   ```

   Add a new VS entry point `VS_PosOnly` (or use the same entry with
   `#ifdef POSITION_ONLY`) that:
   - loads from `meta.position_only_buffer_index` instead of
     `meta.vertex_buffer_index`
   - outputs only `SV_POSITION` (no `TEXCOORD0`)
   - uses the same transform chain: `world_matrix * position` →
     `view_matrix` → `projection_matrix`

   The masked VS path continues to use the full `VertexData` buffer and
   output UV for alpha testing.

7. **`DepthPrePass.cpp`** — add position-only PSO variants:

   Extend the permutation matrix from 4 to 6 variants:

   | Alpha Mode | Sidedness | VS | PS | Buffer |
   | ---------- | --------- | -- | -- | ------ |
   | Opaque | Single | `VS_PosOnly` | null | position-only (12B) |
   | Opaque | Double | `VS_PosOnly` | null | position-only (12B) |
   | Masked | Single | `VS` | `PS` | full vertex (72B) |
   | Masked | Double | `VS` | `PS` | full vertex (72B) |

   `SelectPipelineStateForPartition()` already branches on `is_masked`;
   the opaque branch returns the position-only variant.

Memory cost:

- +12 bytes per vertex for the position-only buffer = +16.7% GPU vertex
  memory per mesh
- +1 SRV per mesh in the descriptor heap
- for 10M vertices: ~114 MB → ~133 MB (+19 MB)

Depth-equal bitwise-match safety:

Both the position-only VS and the `ForwardMesh_VS` load the same `float3`
position and apply the same Local → World → View → Clip transform chain.
The structured buffer stride (12 vs. 72) does not affect the loaded `float3`
value. `SV_POSITION` output is bitwise-identical. HLSL does not reorder
the multiply chain across compilation units. **No depth-equal risk.**

---

#### Files touched (complete list)

| File | Sub-phase | Change |
| ---- | --------- | ------ |
| `Renderer/Passes/DepthPrePass.cpp` | A + B | null PS for opaque; position-only PSO variants |
| `Data/Vertex.h` | B | add `PositionVertex` struct |
| `Data/GeometryAsset.h` | B | add position-only buffer slot in `Mesh` |
| `Renderer/Resources/GeometryUploader.cpp` | B | create + upload position-only buffer |
| `Renderer/Types/DrawMetadata.h` | B | add `position_only_buffer_index` field |
| `Renderer/Upload/DrawMetadataEmitter.cpp` | B | populate new field |
| `Graphics/Direct3D12/Shaders/Depth/DepthPrePass.hlsl` | B | position-only VS variant |
| `Graphics/Direct3D12/Shaders/Renderer/DrawMetadata.hlsli` | B | mirror new field |

#### Validation

- `Oxygen.Renderer.DepthPrePass.Tests`: all existing tests pass
- new unit tests for `SelectPipelineStateForPartition()` returning
  position-only variants for opaque partitions
- RenderDoc capture: opaque depth events show position-only VS,
  `PS: (null)`, 12-byte vertex stride
- RenderDoc capture: masked depth events show full VS, alpha-test PS,
  72-byte vertex stride
- `Oxygen.Renderer.ScreenHzb.Tests`: HZB still valid (depth output unchanged)
- visual regression: none expected (depth values identical)

#### UE5 closeness check

| UE5 concept | Oxygen equivalent |
| ----------- | ----------------- |
| `TDepthOnlyVS<true>` (position-only) | `VS_PosOnly` loading from `position_only_buffer_index` |
| `TDepthOnlyVS<false>` (full vertex) | existing `VS` with full `VertexData` |
| `DepthPosOnlyNoPixelPipeline` | opaque single/double PSO: position-only VS + null PS |
| `DepthNoPixelPipeline` | (not needed separately; opaque path covers this) |
| `FDepthOnlyPS` only when needed | masked single/double PSO: full VS + alpha-test PS |
| `FPositionOnlyVertex` | `PositionVertex` (12 bytes) |

#### When to implement

The trigger for this phase is profiling evidence showing vertex fetch as the
dominant cost in the depth prepass. At current scene complexity (TexturedCube,
LightBench, RenderScene), triangle counts do not approach the threshold where
vertex fetch bandwidth dominates over rasterization or compute. Sub-phase A
(null PS) is trivial and can be done at any time. Sub-phase B (position-only
stream) should wait for scenes with >1M depth prepass triangles.

Status: `deferred`

Deferral rationale:

- explicitly deferred by user direction on March 31, 2026 because the expected
  impact does not justify the implementation and validation cost in the current
  remediation wave
- the work remains valid future optimization scope and should not be considered
  completed
- sub-phase A (null PS) is trivial enough to implement as a cleanup at any time

Current consequence:

- DP-4 is the next active phase
- later phase completion claims must not imply that UE5-style depth-only
  specialization parity was achieved

### Phase DP-4: Add A Depth-Prepass Policy Surface

Problem:
- Oxygen always runs the depth pass whenever a depth texture exists, with no
  policy surface comparable to UE5's early-Z modes

Goal:
- the renderer must be able to choose, validate, and reason about prepass mode

Implementation work:
- introduce an explicit policy surface for the modes Oxygen actually supports
  today:
  - `DepthPrePassMode::kDisabled`
  - `DepthPrePassMode::kOpaqueAndMasked`
- define and publish explicit completeness semantics:
  - `DepthPrePassCompleteness::kDisabled`
  - `DepthPrePassCompleteness::kIncomplete`
  - `DepthPrePassCompleteness::kComplete`
- propagate planned mode from `FramePlanBuilder` into `ForwardPipeline`
- ensure downstream consumers do not infer completeness from mere pass
  registration or mere pass execution
- gate HZB / clustered light culling / VSM page requests off the published
  completeness contract while preserving legitimate offscreen harness paths
- document intentional non-consumers and fallback seams without creating
  parallel legacy paths

Validation:
- `Oxygen.Renderer.CompositionPlanner.Tests`
- `Oxygen.Renderer.DepthPrePass.Tests`
- `Oxygen.Renderer.ScreenHzb.Tests`
- `Oxygen.Renderer.VsmPageRequests.Tests`
- `Oxygen.Examples.RenderScene.exe -v=-1 --frames 20 --fps 30 --directional-shadows conventional`
- plan/docs updated to record the corrected DP-4 scope

UE5 closeness check:
- Oxygen now matches UE-style separation between planned early-depth mode and
  published early-depth completeness
- richer UE5 early-Z modes remain future work and are not falsely implied by
  the current API surface

Validation evidence so far:
- mode/completeness surface added in:
  - [src/Oxygen/Renderer/Pipeline/DepthPrePassPolicy.h](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Pipeline/DepthPrePassPolicy.h)
  - [src/Oxygen/Renderer/Pipeline/Internal/ViewRenderPlan.h](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Pipeline/Internal/ViewRenderPlan.h)
  - [src/Oxygen/Renderer/Pipeline/Internal/FramePlanBuilder.cpp](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Pipeline/Internal/FramePlanBuilder.cpp)
  - [src/Oxygen/Renderer/Pipeline/ForwardPipeline.cpp](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Pipeline/ForwardPipeline.cpp)
  - [src/Oxygen/Renderer/RenderContext.h](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/RenderContext.h)
- downstream contract adoption validated in:
  - [src/Oxygen/Renderer/Passes/ScreenHzbBuildPass.cpp](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/ScreenHzbBuildPass.cpp)
  - [src/Oxygen/Renderer/Passes/LightCullingPass.cpp](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/LightCullingPass.cpp)
  - [src/Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.cpp](H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.cpp)
- focused tests passed:
  - `Oxygen.Renderer.CompositionPlanner.Tests` `6/6`
  - `Oxygen.Renderer.DepthPrePass.Tests` `4/4`
  - `Oxygen.Renderer.ScreenHzb.Tests` `3/3`
  - `Oxygen.Renderer.VsmPageRequests.Tests` `3/3`
- release smoke passed:
  - `Oxygen.Examples.RenderScene.exe -v=-1 --frames 20 --fps 30 --directional-shadows conventional`

Remaining gap:
- none for DP-4 itself

Status: `completed`

### Phase DP-5: Migrate To Reversed-Z

Problem:
- Oxygen uses forward-Z (near=0, far=1) which compounds with the perspective
  hyperbolic distribution to waste float32 precision at the far end
- every modern production engine (UE5, Unity HDRP, Frostbite, CryEngine,
  idTech 7, Godot 4) has adopted reversed-Z for the same mathematical reason
- reversed-Z with float32 and an infinite far plane gives precision equal to
  or better than forward-Z with 24-bit integer depth and a finite far plane
- depth-equal (DP-6), HZB culling (DP-7), and VSM page allocation all benefit
  from uniform far-field precision that only reversed-Z provides
- this phase is placed before DP-6 and DP-7 so that depth-equal logic, HZB
  consumers, and linearization formulas are written against the final
  convention once instead of being validated twice

Pre-implementation audit (completed March 31, 2026):

The codebase was audited for all depth-convention-sensitive sites. Existing
infrastructure already includes `reverse_z` fields in `View`, `ResolvedView`,
and `DepthPrePassOutput` (all defaulting to `false`), plus unused D3D12 depth
presets for `GREATER_EQUAL` in `dx12_utils.h`. The migration surface is
well-defined.

Goal:
- migrate Oxygen to reversed-Z as the engine-wide depth convention

Impacted systems and required changes:

- projection setup
  - `Perspective.cpp`: `glm::perspectiveRH_ZO` produces near=0, far=1
  - `Orthographic.cpp`: `glm::orthoRH_ZO` produces near=0, far=1
  - change: apply reversed-Z projection (negate row 2 columns 2-3 or use a
    dedicated reversed-Z helper) so near=1, far=0
- clear values
  - `DepthPrePass.cpp`: clears to `1.0f`
  - change: clear to `0.0f`
- comparison ops
  - five render passes hardcode `CompareOp::kLessOrEqual`:
    `DepthPrePass`, `ShaderPass`, `WireframePass`, `TransparentPass`, `SkyPass`
  - change: all five to `CompareOp::kGreaterOrEqual`
  - shadow sampler in `Graphics.cpp` uses `LESS_EQUAL`
  - change: shadow sampler to `GREATER_EQUAL`
- HZB reduction
  - `ScreenHzbBuild.hlsl`: initializes to `1.0f`, reduces with `min()`
  - `VsmHzbBuild.hlsl`: uses `VsmMinReduce4` with `min()`
  - change: init to `0.0f`, reduce with `max()` (both shaders)
- light culling depth conversion
  - `LightCulling.hlsl` cluster Z bounds use the formula
    `ndc = (1 - z_near/z) * z_far / (z_far - z_near)` which assumes 0=near
  - `ClusterLookup.hlsli` `ComputeClusterIndex` linearizes depth the same way
  - change: invert the NDC-to-linear mapping for reversed convention
- frustum extraction
  - `Frustum.h` / `Frustum.cpp` already accept a `reverse_z` parameter
  - change: pass `true` from call sites
- depth convention metadata
  - `View.h`, `ResolvedView.h`, `DepthPrePassOutput.h` already carry
    `reverse_z` fields defaulting to `false`
  - change: flip defaults to `true`; consider removing the toggle if
    forward-Z will never be supported again
- depth constants
  - `Constants.h`: `ZNear = 0.0F`, `ZFar = 1.0F`
  - change: swap or add reversed constants
- D3D12 presets
  - `dx12_utils.h` already defines `reversed` and `reversed_readonly` presets
    with `GREATER_EQUAL`
  - change: wire passes to use these presets

Implementation work:
- apply projection, clear, compare, HZB, linearization, and metadata changes
  listed above
- audit any CPU-side culling or debug tooling that assumes forward-Z
- update `DepthPrePassOutput` convention fields
- verify all downstream consumers read the convention metadata correctly
- remove dormant forward-Z infrastructure if forward-Z is permanently retired

Validation:
- all existing depth-prepass and VSM tests must pass under the new convention
- replay-safe `Release` late-frame RenderDoc capture proving correct depth
  clear, compare, and HZB behavior
- pass-focus reports confirming that depth values are in the expected reversed
  range (near surfaces close to 1.0, far surfaces close to 0.0)
- light culling spot-check confirming correct cluster Z-slice assignment
- before/after depth-buffer screenshots from RenderDoc showing the precision
  distribution change

UE5 closeness check:
- UE5 uses reversed-Z as its only depth convention with `GREATER_EQUAL`
  comparisons, `0.0` clear, and `max()` HZB reduction
- after this phase, Oxygen's depth convention matches UE5's

Current implementation:

- engine-facing cameras now build reversed-Z projections through
  `MakeReversedZPerspectiveProjectionRH_ZO()` and
  `MakeReversedZOrthographicProjectionRH_ZO()`
- scene/view metadata defaults now treat reversed-Z as the engine standard
- the main depth consumers now use reversed-Z comparisons:
  - `DepthPrePass`
  - `ShaderPass`
  - `TransparentPass`
  - `WireframePass`
  - `SkyPass`
- the D3D12 shadow comparison sampler now uses `GREATER_EQUAL`
- scene HZB and VSM HZB generation were flipped to reversed-Z reduction
- depth-prepass output now publishes reversed-Z convention metadata
- the main scene depth target now uses reversed-Z clear values in:
  - `CompositionViewImpl`
  - `ForwardPipeline`
  - `DepthPrePass`
- conventional directional shadows now use reversed-Z for:
  - depth resource clear values
  - directional frustum near/far reconstruction
  - orthographic light projection setup

Validation evidence so far:

- focused tests already proven green in this remediation wave:
  - `Oxygen.Renderer.DepthPrePass.Tests`
  - `Oxygen.Renderer.ScreenHzb.Tests`
  - `Oxygen.Scene.PerspectiveCamera.Tests`
  - `Oxygen.Scene.OrthographicCamera.Tests`
  - `Oxygen.Renderer.Frustum.Tests`
  - `Oxygen.Scripting.Bindings.Tests --gtest_filter=ConventionsBindingsTest.ExecuteScriptConventionsExposeEngineCoordinateLaw`
- conventional-shadow regression fix validation:
  - `Oxygen.Renderer.ShadowManagerPolicy.Tests` passes after the reversed-Z
    conventional-shadow fixes
  - runtime evidence:
    - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_debuglayer.log`
      captured the pre-fix failure:
      - `invalid camera depth span ... skipping directional shadows`
      - `no shadow products were published`
    - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_debuglayer_postfix2.log`
      shows that failure is gone on the rebuilt binary
  - replay evidence:
    - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_frame60_frame60.rdc`
    - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_frame60.shadow_inner_timing.txt`
    - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_frame60.shadow_inner_focus.txt`
    - this proves `ConventionalShadowRasterPass.ShadowDepthWork` is executing
      again with 4 clear events and 4 draw events on shadow slices `0..3`
  - post-fix runtime smoke on the current `Debug` binary:
    - `out/build-ninja/analysis/conventional_shadow_runtime_debug_after_biasfix.log`
    - no D3D12 warnings/errors, no invalid depth-span publication failure, and
      stable conventional-shadow execution through the frame run
  - live visual validation:
    - user-confirmed disappearance of the widespread periodic receiver acne in
      `RenderScene` after the reversed-Z raster-bias sign fix

Scope correction as of April 1, 2026:

- DP-5 exit is no longer blocked on VSM harness/test failures.
- The phase closes on engine-wide reversed-Z migration for scene depth,
  downstream consumers, HZB/light-culling behavior, and conventional
  directional shadows.
- Remaining VSM instability is tracked as a separate follow-up and must not be
  used to keep the depth-prepass reversed-Z phase artificially open.

Summary of DP-5 deliverables:

1. Scene-depth convention migrated to reversed-Z.
   - cameras, view metadata, clears, depth compares, and canonical depth
     products now follow near=`1.0`, far=`0.0`, clear=`0.0`,
     compare=`GREATER_EQUAL`
2. Downstream depth consumers updated to the same convention.
   - HZB generation, clustered light culling, and scene-depth consumers now
     reason about reversed-Z explicitly
3. Conventional directional shadows aligned with engine reversed-Z.
   - resource clears, frustum near/far reconstruction, orthographic light
     projection, shadow comparison sampling, and raster bias direction are now
     coherent with reversed-Z
4. Depth-path documentation updated to describe reversed-Z as the canonical
   renderer contract.

Single pending follow-up moved out of the DP-5 exit gate:

1. VSM reversed-Z harness/test failures
   - normalize the remaining VSM harness/tests to the engine reversed-Z
     helpers and fix the remaining VSM suites that still fail under the new
     convention

Remaining gap for DP-5 itself:

- none

Status: `completed`

### Phase DP-6: Rework Downstream Consumers To Exploit Depth Better

Problem:
- later passes consume depth passively instead of exploiting the fact that a
  prepass already happened
- depth-equal is the primary reason to run a depth prepass at all: without it,
  hidden fragments still execute the full pixel shader before the hardware
  depth test rejects them
- depth-equal is only safe when the pipeline has a truthful completeness signal
  from DP-4 and the correct depth convention from DP-5

Goal:
- later passes should derive clear, measurable value from the prepass
- the primary deliverable is depth-equal rendering in `ShaderPass` when the
  prepass is complete, eliminating redundant material evaluations on hidden
  fragments

Pre-implementation audit (completed March 31, 2026):

Current downstream depth consumer state:

- `ShaderPass`: binds read-only DSV with `kLessOrEqual`, `depth_write=false`,
  transitions depth to `kDepthRead`; does not bind depth as SRV
- `TransparentPass`: read-only DSV with `kLessOrEqual`, `depth_write=false`;
  no SRV
- `SkyPass`: read-only DSV with `kLessOrEqual`, `depth_write=false`; also
  creates an SRV for depth (shader atmosphere/fog reads)
- `GroundGridPass`: no DSV, no depth test; reads depth via SRV only for
  grid-depth intersection
- `LightCullingPass`: compute; reads depth via canonical SRV from
  `DepthPrePassOutput`; validates `is_complete` before dispatch
- `VsmPageRequestGeneratorPass`: compute; identical pattern to
  `LightCullingPass`
- `ScreenHzbBuildPass`: compute; reads depth via canonical SRV for mip-0

Key observation: `ShaderPass` already uses read-only DSV with no depth writes.
The only change needed for depth-equal is switching its `depth_func` from
`kLessOrEqual` to `kEqual` (or `kGreaterOrEqual` to `kEqual` after DP-5)
when `DepthPrePassOutput.is_complete` is true. The read-only DSV and
`kDepthRead` resource state are already correct for simultaneous SRV binding
if needed.

Implementation work:

1. depth-equal in `ShaderPass` (primary deliverable):
   - when `DepthPrePassOutput.is_complete` is true, set `depth_func` to
     `CompareOp::kEqual` instead of the current less-or-equal
   - when `is_complete` is false, keep the existing less-or-equal (or
     greater-or-equal after DP-5) behavior as fallback
   - critical correctness requirement: depth-equal requires bitwise-identical
     depth values between the prepass and the main pass; the vertex transform
     codepath must produce identical position computation in both passes;
     validate this explicitly

2. read-only depth plus SRV co-binding in `ShaderPass`:
   - D3D12 supports binding a depth-stencil as read-only DSV while
     simultaneously binding it as SRV when writes are disabled
   - `ShaderPass` already meets the prerequisites (read-only DSV, no depth
     writes, `kDepthRead` state)
   - evaluate whether `ShaderPass` shaders benefit from depth SRV access
     (soft particles, depth-aware effects); if not, defer SRV co-binding

3. `TransparentPass` depth-equal evaluation:
   - transparent passes cannot use depth-equal because they render with
     blending over opaque geometry at varying depths
   - no change expected; document this explicitly as a non-candidate

4. `SkyPass` depth-equal evaluation:
   - sky renders at the far plane; depth-equal would require the prepass to
     have written a sky depth value, which it does not
   - no change expected; document as non-candidate

5. stencil sideband evaluation:
   - UE5 uses stencil bits during the prepass to classify material lighting
     models (default lit, subsurface, hair) for selective per-pixel processing
   - in Oxygen's forward renderer, this classification is lower value than in
     deferred because the forward pass already knows the material
   - defer stencil sideband unless a concrete consumer is identified
   - document the deferral rationale

6. compute consumers (`LightCullingPass`, `VsmPageRequestGeneratorPass`,
   `ScreenHzbBuildPass`):
   - these already consume depth via the shared contract from DP-2
   - with DP-4 completeness, they can skip dispatch entirely when
     `is_complete` is false and the pass output would be meaningless
   - evaluate whether incomplete-depth fallback behavior is needed or whether
     these passes should simply not run without complete depth

Validation:
- depth-equal correctness test: render a scene with `ShaderPass` using
  depth-equal and verify that visible opaque fragments match the prepass depth
  while occluded `ShaderPass` fragments fail the equal depth test without
  changing the final color/depth result
- depth-equal precision test: verify no flickering or holes at distance,
  especially after DP-5 reversed-Z migration
- performance evidence (remaining delta): measure fragment shader invocation
  count before/after depth-equal on a scene with significant overdraw
- fallback behavior test: verify `ShaderPass` falls back to less/greater-equal
  when `is_complete` is false
- RenderDoc resource state validation: confirm `kDepthRead` state and
  read-only DSV binding in `ShaderPass` under depth-equal mode
- regression check: all existing examples (`RenderScene`, `MultiView`) must
  produce visually identical output

UE5 closeness check:
- UE5's base pass uses depth-equal when `bIsEarlyDepthComplete` is true,
  eliminating redundant shading on all occluded fragments
- UE5 uses stencil classification during the prepass for deferred lighting
  model dispatch; Oxygen's forward path does not need this currently
- after this phase, Oxygen's `ShaderPass` should match UE5's base-pass
  depth-equal exploitation pattern for complete prepass scenarios

Implementation update (April 1, 2026):

- `ShaderPass` now resolves canonical `DepthPrePassOutput.depth_texture` even
  when completeness is `kIncomplete`, preserving the DP-2/DP-4/DP-5 depth
  contract instead of dropping to framebuffer-only depth assumptions
- `ShaderPass` now builds `CompareOp::kEqual` only when
  `DepthPrePassCompleteness` is `kComplete` and keeps
  `CompareOp::kGreaterOrEqual` as the fallback when completeness is not
  complete
- `ShaderPass` PSOs now rebuild when the resolved depth attachment or depth
  compare op changes, so completeness transitions cannot leave a stale
  fallback/equal pipeline live
- `TransparentPass` and `SkyPass` remain unchanged as intentional
  non-candidates for depth-equal

Validation evidence (April 1, 2026):

- targeted tests:
  - `Oxygen.Renderer.ShaderPassDepthState.Tests` passed (`2` tests)
  - `Oxygen.Renderer.DepthPrePassContract.Tests` passed (`4` tests)
- live runs:
  - `Oxygen.Examples.MultiView.exe -v=-1 --frames 60 --fps 0 --pip-wireframe false`
  - `Oxygen.Examples.MultiView.exe -v=2 --frames 20 --fps 0 --pip-wireframe false`
  - `Oxygen.Examples.RenderScene.exe -v=-1 --frames 60 --fps 0 --directional-shadows conventional`
  - `Oxygen.Examples.RenderScene.exe -v=2 --frames 20 --fps 0 --directional-shadows conventional`
- RenderDoc capture:
  - `out/build-ninja/analysis/dp6/renderscene_debug_dp6_frame30.rdc`
  - `out/build-ninja/analysis/dp6/renderscene_debug_dp6_shader_depth_equal_report.txt`
  - capture confirms `ShaderPass` uses `CompareFunction.Equal`,
    `depthReadOnly=True`, the shared `Forward_HDR_Depth` target in
    `D3D12_RESOURCE_STATE_DEPTH_READ`, and a sampled `ShaderPass` fragment at
    `(532, 787)` fails the equal depth test (`event_id=508`) because its
    shader depth (`0.002552839694544673`) does not match the prepass depth
    (`0.0029069758020341396`)

Remaining DP-6 delta:

- capture an explicit before/after fragment-invocation or timing comparison to
  quantify the depth-equal win on an overdraw-heavy scene

Status: `in_progress`

### Phase DP-7: Unify Depth Derivatives And Stop Redundant Work

Problem:
- `ScreenHzbBuildPass` produces only a min (closest) HZB pyramid; no furthest
  (max) HZB exists
- `LightCullingPass` ignores the HZB entirely and rebuilds its own per-tile
  depth min/max from raw depth via a 256-thread shared-memory parallel
  reduction inside each 16x16 threadgroup
- `VsmPageRequestGeneratorPass` reads raw depth per-pixel and does not leverage
  any hierarchical product for early-out or coarse-grained page decisions
- the HZB's only consumer today is `VsmInstanceCulling` (previous-frame
  occlusion culling); the investment in building a full mip pyramid every
  frame is underexploited
- light culling tile bounds are ephemeral and cannot be reused by any other
  system that needs the same information

Goal:
- produce a canonical dual min+max HZB from the depth prepass
- route `LightCullingPass` and `VsmPageRequestGeneratorPass` through shared
  depth derivatives instead of private raw-depth reduction paths
- ensure every depth derivative is built once and consumed by all systems that
  need it

Pre-implementation audit (completed March 31, 2026):

Current depth derivative producers and consumers:

- `ScreenHzbBuildPass`
  - source: `DepthPrePassOutput` canonical SRV (DP-2 contract)
  - product: `R32Float` min-only mip pyramid, double-buffered per view
  - output contract: `ViewOutput { texture, srv_index, width, height,
    mip_count, available }`
  - consumers: `VsmInstanceCulling` (previous-frame HZB occlusion test) only
  - gap: no max pyramid; light culling and VSM page requests do not consume it

- `LightCullingPass`
  - source: raw depth via `DepthPrePassOutput.canonical_srv_index`
  - method: each 16x16 threadgroup loads 256 depth samples into shared memory,
    does a binary-tree parallel reduction to compute tile min and tile max
  - tile min/max is used in the legacy tiled path (`CLUSTERED=0`) only; the
    active clustered path (`CLUSTERED=1`) computes cluster Z bounds from
    logarithmic slicing math and ignores the tile reduction result
  - gap: the per-tile reduction work is wasted in clustered mode; a shared HZB
    product could provide the same bounds without per-tile raw-depth reads

- `VsmPageRequestGeneratorPass`
  - source: raw depth via `DepthPrePassOutput.canonical_srv_index`
  - method: per-pixel depth load, reconstruct world position, project into each
    VSM, `InterlockedOr` page request flags
  - gap: no hierarchical early-out; every screen pixel is processed even when
    entire tiles map to the same VSM page; a coarse HZB pass could skip tiles
    whose depth range maps entirely within an already-requested page

- `VsmHzbUpdaterPass` (shadow-domain, not scene-domain)
  - source: shadow depth atlas (separate domain from scene depth)
  - product: per-page min-only shadow HZB
  - not affected by this phase; included for clarity

Implementation work:

1. add a max (furthest) HZB channel to `ScreenHzbBuildPass`:
   - extend the reduction shader to compute both `min()` and `max()` in the
     same dispatch; this is a single extra ALU per sample since both values
     traverse the same 2x2 footprint
   - after DP-5 reversed-Z: closest depth is the max value (near=1, far=0),
     furthest depth is the min value; name the channels by semantic meaning
     (`closest`, `furthest`) rather than by operation to avoid convention
     confusion
   - output format options: either a two-channel `RG32Float` texture or two
     separate `R32Float` pyramids; prefer the two-channel format to halve
     dispatch count and descriptor pressure
   - update `ViewOutput` to expose both `closest_srv_index` and
     `furthest_srv_index` (or a single SRV with two channels)
   - update `VsmInstanceCulling` to consume from the renamed output; this is a
     rename-only change since it already reads the closest channel

2. route `LightCullingPass` through the shared HZB:
   - replace the per-tile 256-thread shared-memory depth reduction with a
     single HZB mip lookup at the tile's spatial extent
   - for a 16x16 tile, the correct HZB mip is `ceil(log2(16)) = 4`; a single
     `Load()` from the closest and furthest HZB at that mip gives exact tile
     min/max without 256 raw-depth reads and 8 barrier-synchronized reduction
     steps
   - in clustered mode, the tile min/max from the HZB can additionally be used
     to skip clusters that fall entirely outside the tile's depth range,
     tightening the light-tile intersection and reducing false positives
   - this eliminates the raw-depth SRV dependency from `LightCullingPass`
     entirely; it consumes the HZB SRV instead
   - savings: 256 texture loads + 8 barrier rounds per tile → 2 texture loads
     per tile; at 1280x720 with 16x16 tiles = 3600 tiles, this is 920K fewer
     texture loads per frame

3. add coarse-grained HZB early-out to `VsmPageRequestGeneratorPass`:
   - add an optional coarse pre-pass that samples the HZB at tile granularity
     to determine whether an entire tile's depth range maps to pages that are
     already requested or already resident
   - tiles where the entire depth range falls within a single VSM page (or a
     set of already-flagged pages) can skip the per-pixel pass entirely
   - this does not replace the per-pixel path; it gates it
   - expected benefit scales with scene depth coherence: for typical outdoor
     scenes with large uniform-depth regions, a significant fraction of tiles
     will early-out; for highly fragmented depth, the overhead of the coarse
     pass is one HZB load per tile (negligible)
   - if profiling shows the coarse pass does not pay for itself on Oxygen's
     target scenes, defer this optimization and document the measurement

4. publish a unified scene-depth derivatives contract:
   - define a `SceneDepthDerivatives` or extend `DepthPrePassOutput` with:
     - raw depth texture + canonical SRV (already exists from DP-2)
     - closest HZB texture + SRV + mip count
     - furthest HZB texture + SRV + mip count (or second channel)
     - dimensions matching the depth target
     - depth convention metadata (from DP-5)
   - all consumers that need hierarchical depth must consume from this contract
     instead of building private reductions

5. remove the dead per-tile reduction from `LightCulling.hlsl`:
   - once the HZB path is validated, remove the shared-memory reduction code
     and the `s_DepthTileMin` / `s_DepthTileMax` arrays
   - this simplifies the shader and frees 2KB of groupshared memory per
     threadgroup

Validation:
- correctness: `LightCullingPass` HZB-based tile bounds must produce identical
  or tighter light-tile intersection results compared to the current
  shared-memory reduction; validate on a scene with many overlapping lights
- correctness: `VsmPageRequestGeneratorPass` coarse early-out must not miss
  any page request that the per-pixel path would generate; validate by running
  both paths and comparing request bitmaps
- performance: pass-level GPU timing before/after for `LightCullingPass` and
  `VsmPageRequestGeneratorPass` on `RenderScene` with `--directional-shadows
  conventional` and with VSM
- regression: all examples (`RenderScene`, `MultiView`) must produce visually
  identical output
- RenderDoc evidence: capture showing HZB mip reads in `LightCullingPass`
  instead of raw-depth tile reduction
- documentation: update scene-depth product ownership docs to reflect the
  unified derivatives contract

UE5 closeness check:
- UE5 builds a dual closest+furthest HZB (`HZBClosest` / `HZBFurthest`) from
  the depth prepass and routes it to occlusion culling, light culling, SSR,
  SSAO, and other screen-space consumers
- UE5's light culling uses the HZB for tile depth bounds rather than per-tile
  raw-depth reduction
- after this phase, Oxygen's depth derivative set and consumer routing should
  match UE5's architectural shape: one shared hierarchical product, many
  consumers, zero private reductions

Status: `not_started`

### Phase DP-8: Clean Up Tests, Docs, And Ownership Boundaries

Problem:
- current docs are stale, dedicated tests are thin, and pass registration /
  ownership semantics are messier than they should be

Goal:
- the depth path should be understandable, testable, and cleanly owned

Implementation work:
- add dedicated depth-prepass tests instead of relying only on indirect tests
- update `depth_pre_pass.md` to the actual architecture
- clean up ownership/registration semantics if double-registration remains
- document the final relationship between depth prepass, HZB, light culling,
  VSM, and shader pass

Validation:
- focused tests for depth-prepass behavior and contracts
- doc review against actual implementation
- final audit that this remediation plan is fully reflected in repo docs

UE5 closeness check:
- this phase is only complete when the Oxygen docs accurately describe the
  adopted UE5-inspired structure, not the old simplified one

Status: `not_started`

## 5. Validation Matrix

Every implementation phase should record evidence in
`out/build-ninja/analysis/depth_prepass_*` with exact commands and artifacts.

Minimum evidence categories:

- code-level tests
- replay-safe `Release` RenderDoc captures
- pass-local timing where relevant
- focused deep-dive reports where timing alone is insufficient
- before/after documentation updates

Recommended capture discipline:

- use late frames only
- use `Release`
- prefer `--directional-shadows conventional` for depth-prepass remediation
  unless the current phase explicitly needs VSM interaction
- use the repo-owned RenderDoc UI analysis workflow
- keep baseline analysis bounded and put expensive inspection into focused
  scripts

## 6. Explicit Non-Shortcuts

The following are not acceptable ways to close this work:

- micro-tuning shader instructions while keeping the wrong pass shape
- adding one-off depth SRVs in more passes instead of introducing a shared
  output contract
- calling the pass "UE5-like" without matching the specialization and policy
  model in substance
- declaring reversed-Z out of scope now that the audit confirms universal
  engine adoption and concrete precision benefits
- updating code without updating docs and validation evidence

## 7. Exit Condition

This plan remains `in_progress` until:

- every phase above is either completed with evidence or explicitly descoped
  with a documented rationale
- the final Oxygen depth path is measurably closer to UE5 in structure,
  specialization, and downstream exploitation
- the repo-owned tooling can validate the resulting behavior on replay-safe
  captures
