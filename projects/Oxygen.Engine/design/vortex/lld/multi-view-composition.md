# Multi-View and Multi-Surface Composition LLD

**Milestone:** VTX-M06A - Multi-View Proof Closeout
**Deliverable:** D.17 - Multi-view composition LLD
**Roadmap Status:** `validated`
**LLD Status:** `review-addressed; implementation validated`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a Vortex-native system
  that targets maximum parity with UE5.7, grounded in local source under
  `F:\Epic Games\UE_5.7\Engine\Source` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## 1. Scope Correction

M06A is not "render a secondary PiP over the main scene." PiP is one trivial
composition shape inside the required system. The target is a renderer that can
render multiple independent scene views and compose them into one or more
presentation surfaces without duplicating frame-wide GPU data, leaking global
render settings across views, or turning editor overlays into ad hoc late UI.

The most demanding baseline scenario is an editor layout:

- one, two, three, or four simultaneously visible scene viewports
- each viewport can have a different camera and projection
- each viewport can have a different render mode: lit, solid, wireframe,
  unlit, material/debug visualization, shadow mask, scene-depth view, etc.
- each viewport can have depth-aware world overlays: gizmos, debug lines,
  frustums, icons, selection outlines, bounds, light shapes
- each viewport can have screen-space overlays: stats, labels, rulers,
  viewport badges
- the editor frame can target one surface, multiple windows/surfaces, or
  offscreen surfaces used as textures elsewhere

The implementation may land in phases, but the design must not bake in the
current single-scene-view cursor or a PiP-only output model.

## 2. UE 5.7 Source Parity Findings

The local UE 5.7 source establishes the design shape Vortex should follow.

| UE 5.7 source | Finding | Vortex implication |
| --- | --- | --- |
| `Runtime/Engine/Public/SceneView.h:2211,2308,2311` (`FSceneViewFamily`, `Views`, `AllViews`) | A render family carries render target, scene, show flags, view mode, frame counters, and an array of primary views. `AllViews` also includes scene-capture/custom-pass views. | Vortex needs a view-family/render-batch concept above individual `CompositionView`s, with a first-class auxiliary-view path. Family state is not a single view. |
| `Runtime/Engine/Public/SceneView.h:1429,2099` (`FSceneView::State`, `EyeAdaptationViewState`) | Persistent history is hung off producer-provided view state pointers, not inferred from render order or transient renderer arrays. | `CompositionView` needs an opaque producer-owned `ViewStateHandle`; `ViewId` is not enough for temporal ownership. |
| `Runtime/Renderer/Private/SceneRendering.cpp:880,893,2645,3034` (`FViewInfo`, `FSceneRenderer`) | UE copies each `FSceneView` into an internal `FViewInfo`, validates unique view state for occlusion, and builds renderer-owned `Views`/`AllViews`. | Vortex should materialize immutable public view intent into renderer-owned per-frame view packets with unique per-view state/history references. |
| `Runtime/Renderer/Private/SceneRendering.cpp:3005,3034,3040,5026` (`FCustomRenderPassInfo`, `AllViews`) | UE can inject custom-render-pass / scene-capture views into one renderer and include them in `AllViews`. | Vortex must distinguish primary, auxiliary, and composition-only views before implementation starts. |
| `Runtime/Renderer/Private/DeferredShadingRenderer.cpp` | UE runs a mix of family-wide work and loops over `Views` for view-local work. Examples include shared light-grid preparation, per-view ray tracing setup, per-view TSR inputs, and per-view translucency resource maps. | Vortex stage scheduling must distinguish frame/family work from per-view work. It must not simply call a full renderer once per PiP layer. |
| `Runtime/Engine/Private/GameViewportClient.cpp` and `Runtime/Engine/Public/SceneViewExtension.h:140,145,160,175,222,227` | Game view rendering builds a `FSceneViewFamilyContext`, gathers view extensions, runs `SetupViewFamily`, then calls `BeginRenderingViewFamily` and render-thread view hooks. | Vortex needs explicit pre-render family assembly and typed extension/overlay hooks before scene rendering starts. |
| `Editor/UnrealEd/Private/EditorViewportClient.cpp` and `LevelEditorViewport.cpp` | Editor viewport clients own show flags and view modes, call `BeginRenderingViewFamily`, then draw canvas, debug services, widgets, stats, and PDI/editor-mode primitives. | Vortex must make render mode, debug mode, and overlay lanes per view. Global frame settings are insufficient for editor parity. |
| `Runtime/Renderer/Private/BasePassRendering.cpp:1125,1132,1262,1417` | Wireframe/shader-complexity/editor primitives can change render-pass topology and use editor primitive color/depth targets. | Vortex render/debug modes need an explicit split-vs-permutation classification and editor-primitive target contract. |
| `Runtime/Engine/Public/SceneTexturesConfig.h:113,161,167,170,201` | Scene texture configuration records extent, sample count, editor primitive sample count, depth aux, and feature requirements. | Vortex scene-texture resources need descriptor-keyed pooling/leases, editor-primitive dimensions, and explicit setup requirements. |
| `Runtime/Engine/Public/SceneView.h:2163,2170,2417,2498,2573,2608,2612` | Screen percentage, secondary view fraction, and primary spatial upscaler are view-family concerns in UE. | M06A must at least pin a static resolution model even though dynamic resolution/TSR are deferred. |

These findings are source-derived. No internet source is used for this LLD.

## 3. Design Goals

1. Support N scene views per frame and M output surfaces per frame.
2. Preserve one Vortex renderer architecture; do not create an editor renderer,
   PiP renderer, or offscreen renderer fork.
3. Upload frame-wide data once per scene/frame where it is view-independent:
   geometry, materials, transforms, global light records, shadow-map products
   that are independent of the camera view.
4. Keep view-local data isolated and keyed by published `ViewId`: prepared draw
   metadata, view constants, scene-texture bindings, HZB, occlusion results,
   histories, exposure, debug mode, overlay batches, and final view output.
5. Pool scene-texture families and intermediate outputs by descriptor. Do not
   allocate a permanent full GBuffer family for every possible viewport when
   lifetimes do not overlap.
6. Compose outputs through an explicit surface plan. One view can feed multiple
   surfaces, and one surface can contain multiple views.
7. Make validation capture-friendly: every pass, view, family, and surface must
   be identifiable in diagnostics and RenderDoc.
8. Preserve the implementation path for scene captures, planar reflections, and
   custom render passes by modeling auxiliary views now, even if their first
   producers land after M06A.
9. Avoid hidden temporal ownership. Persistent history is owned by the view
   producer through an explicit handle, not by reusing a numeric view id.

## 4. Existing Vortex Substrate

The current Vortex code already has useful anchors:

- `CompositionView` expresses view intent: view id, viewport/scissor, z-order,
  camera, HDR policy, environment flags, exposure source, `force_wireframe`,
  optional `ShadingMode`, and late overlay callback.
- `CompositionView::exposure_source_view_id` currently enforces current-frame
  source-before-consumer ordering. That is a known M06A correction point, not
  the target contract.
- `ViewLifecycleService` sorts active views by z-order/submission order, owns
  `CompositionViewImpl` instances, resolves cameras, publishes runtime
  `ViewContext`s, and validates exposure source order.
- `CompositionViewImpl` owns per-view HDR/SDR intermediate resources today.
- `FramePlanBuilder` creates `FrameViewPacket`s and currently resolves render
  mode, tone map policy, sky flags, and depth-prepass policy from mostly
  frame-global settings.
- `RenderContext` already has `frame_views`, `active_view_index`, and
  `current_view`.
- `RenderContext::current_view` is currently an ambient mutable cursor. M06A
  must constrain it through a typed per-view scope before adding a multi-view
  production loop.
- `InitViewsModule` is already designed to iterate `RenderContext::frame_views`
  and publish one `PreparedSceneFrame` per eligible scene view.
- `CompositionPlanner` already converts sorted frame view packets into
  composition tasks, and `Renderer` owns composition submission queueing and
  execution.
- `CompositionPlanner` currently treats `kZOrderScene` plus opacity 1 as a
  "primary scene copy" case. M06A must replace this with a structural surface
  layer predicate.
- `design/vortex/ARCHITECTURE.md` sections 5 and 6.2 remain the higher-level
  authority for frame dispatch and per-view versus per-frame stage separation.

Slice C replaced the initial blocking limitation where
`Renderer::PopulateRenderContextViewState` selected one active scene-view cursor
and `SceneRenderer::OnRender` executed only that selected view. The current M06A
runtime path materializes frame entries without selecting a cursor, enters
`PerViewScope` inside `SceneRenderer::RenderViewFamily`, and keeps the
single-current-view harness path available for tests and tools.

## 5. Core Concepts

### 5.1 Surface

A surface is a final output target or offscreen presentation target. It is not
a scene view. A surface has:

- stable surface identity
- current framebuffer/backbuffer
- extent and format
- clear/load policy
- presentability policy
- frame ownership/fence lifetime

Renderer Core owns surface composition submission. The graphics backend owns
swapchains and the concrete presentation operation.

### 5.2 View Intent

`CompositionView` remains the public view intent. It describes what should be
rendered or overlaid, not which transient scene-texture allocation will be used.
For M06A it must grow, directly or through an adjacent settings payload, to
carry per-view:

- render mode (`Solid`, `Wireframe`, `OverlayWireframe`, future modes)
- shader debug mode / visualization mode
- show-flag-like feature controls for editor/tooling views
- overlay channels
- composition routing to one or more surfaces

Frame-global render/debug settings may remain as default inputs, but they must
not be the only source of truth once multiple editor viewports are active.

### 5.3 View Packet

A view packet is the renderer-owned, per-frame realization of view intent:

- intent `ViewId`
- published runtime `ViewId`
- `ViewKind`
- optional producer-owned `ViewStateHandle`
- resolved camera/projection
- `ViewRenderPlan`
- view constants
- prepared-scene pointer
- render target/composite source
- scene-texture lease
- overlay batches
- final view output reference

This is Vortex's analogue to UE's copy from `FSceneView` into renderer-owned
`FViewInfo`.

### 5.4 View Kind and Auxiliary Views

Every packet declares a kind:

| Kind | Scene stages | Surface routing | Examples |
| --- | --- | --- | --- |
| `Primary` | yes | yes | game viewport, editor perspective/top/side/front views |
| `Auxiliary` | yes | optional/no direct present | scene capture, planar reflection, custom render pass, render-to-texture view consumed by another view |
| `CompositionOnly` | no | yes | UI layer, surface overlay layer, imported texture layer |

`Primary` and `Auxiliary` packets enter view-family batching.
`CompositionOnly` packets bypass scene batching and feed the composition
planner directly.

The render-batch model mirrors UE's `Views` / `AllViews` split:

- `surface_views`: primary scene views routed to a surface
- `auxiliary_views`: scene views that produce extracted textures or other
  mid-frame products
- `all_scene_views`: `surface_views + auxiliary_views` in deterministic render
  dependency order
- `composition_only_packets`: non-scene packets used only by the surface
  composition plan

Auxiliary outputs must be extracted before a scene-texture lease is released.
If a primary view samples an auxiliary view output in the same frame, the
auxiliary view is a dependency edge in the batch graph and renders first. M06A
does not need to ship a full scene-capture component, but the packet kind and
dependency edge are required before slice 1 implementation starts.

Auxiliary products are published through typed bindings, not by leaking raw
framebuffer pointers into materials. A material or stage that consumes a
same-frame auxiliary result reads an `AuxiliaryViewBindings`-style product
created after extraction and before the dependent primary pass begins.

Auxiliary IO is described by stable ids:

```cpp
using AuxOutputId = NamedType<std::uint64_t, struct AuxOutputIdTag>;

enum class AuxOutputKind : std::uint8_t {
  kColorTexture,
  kDepthTexture,
  kSceneTextureBinding,
  kCustomPassProduct,
};

struct AuxOutputDesc {
  AuxOutputId id;
  AuxOutputKind kind;
  std::string_view debug_name;
  bool same_frame_required { true };
};

struct AuxInputDesc {
  AuxOutputId id;
  AuxOutputKind kind;
  bool required { true };
};
```

`CompositionView` intent may declare `produced_aux_outputs` and
`consumed_aux_outputs`; `FrameViewPacket` carries the resolved descriptors plus
producer/consumer dependency edges. `AuxOutputId` is stable for the producer
handle, not for a transient framebuffer allocation. A missing required producer
is a validation failure before rendering starts. A missing optional producer
publishes a typed invalid binding and records diagnostics.

### 5.5 View Persistent State Owner

Per-view persistent state is owned by the view producer, not by Renderer Core.
`CompositionView` carries an opaque `ViewStateHandle` whose lifetime is bound to
the producer: editor viewport client, scene-capture component, game viewport, or
headless harness. This matches UE's `FSceneView::State` /
`EyeAdaptationViewState` pattern.
Renderer Core consumes the handle by reference. It must not allocate persistent
state for a view that did not provide a valid handle.

Persistent state includes:

- exposure/eye-adaptation history
- previous view matrices and jitter state
- temporal AA / post-process history
- previous HZB or occlusion history
- persistent view output when intentionally retained
- overlay/hit-test state needed by tools

`ViewId` remains the publication and composition identity for the current frame.
It is not the temporal ownership key. Reusing a `ViewId` with a different
`ViewStateHandle` starts fresh history. A packet with no state handle is
stateless: it may render, but it cannot consume or update temporal products.

Eviction and invalidation:

| Trigger | Required behavior |
| --- | --- |
| producer drops/unregisters handle | release all service state keyed by that handle after GPU fences retire |
| scene-texture descriptor key changes | invalidate histories whose dimensions/formats/sample count no longer match |
| render/debug mode changes to a history-incompatible mode | reset affected histories before the view renders |
| producer marks handle transient/stateless | do not allocate temporal state |
| optional idle trimming for service caches | release only GPU resources, never reinterpret a new producer as the old handle |

Renderer-owned LRU keyed only by `ViewId` is rejected. It is simpler in the
short term, but it leaks history across reused ids and makes editor viewport
lifetime ambiguous.

### 5.6 View Family / Render Batch

A render batch groups scene views that can be prepared and rendered together.
It is the Vortex analogue to `FSceneViewFamily` at the design level. A batch is
keyed by compatibility, not by z-order:

- same scene
- same frame sequence and time domain
- compatible renderer feature set/capability set
- compatible scene-texture pool key class. This means compatible allocation
  descriptors for pooling/reuse; it never means two active views write the same
  physical texture concurrently.
- compatible environment/shadow frame products where those products can be
  reused
- compatible output color/depth requirements

Different final surfaces can consume outputs from the same render batch.
Different render modes/debug modes can exist in the same batch if the stage
pipeline supports per-view plan selection and correct PSO/permutation selection.
If a mode fundamentally changes required attachments or stage topology, the
batch splitter must separate it.

Auxiliary dependencies are allowed to cross render-batch boundaries. Forcing
producer and consumer into the same batch is rejected because incompatible
attachment topology or feature masks would then corrupt the batch key. Renderer
Core builds an outer `ViewBatchGraph`:

- node: one `ViewRenderBatch`
- edge: batch A must render before batch B because A produces an
  `AuxOutputId` consumed by B
- validation: cycles and missing required producers fail before GPU work
- execution: topologically sort batches, then sort views inside each batch

If the producer and consumer are in the same batch, the edge is handled by the
batch's `all_scene_views` topological order. If they are in different batches,
the producer batch must extract the auxiliary product and publish the binding
before the consumer batch begins.

The M06A render batch is not UE instanced stereo / mobile multiview. It is a
CPU-side scheduling and lifetime construct. XR/mobile multiview remains
deferred and would need separate shader, attachment-array, and view-uniform
rules.

### 5.7 Composition Plan

A composition plan maps completed view outputs and overlay outputs into one or
more surfaces. It is not the same as render order. It contains:

- surface id and target framebuffer
- ordered layer list
- source view output or source texture
- destination viewport/rect
- source rect if applicable
- blend/copy mode
- alpha/opacity
- color-space conversion policy
- clear/load/present policy

Sorting is deterministic: surface order, then layer z-order, then submission
order. Stable ordering is required even when several layers share z-order.

## 6. Ownership Boundaries

| Responsibility | Owner |
| --- | --- |
| View intent API and runtime publication | Renderer Core |
| View persistent-state handle lifetime | View producer |
| View lifecycle, sorted active views, exposure-source handle validation | Renderer Core / `ViewLifecycleService` |
| View-family/render-batch construction | Renderer Core |
| Per-view view constants and `ViewFrameBindings` publication | Renderer Core |
| Scene stage ordering and scene-texture usage policy | `SceneRenderer` |
| Stage-specific rendering logic | Stage modules |
| Lighting/shadow/environment/post-processing domain products | Subsystem services |
| Scene-texture concrete resource family and leases | `SceneRenderer` through a Vortex-native scene-texture pool |
| Composition task planning, queueing, execution, and surface handoff | Renderer Core |
| Graphics resources, command recorders, barriers, queues, swapchains | Oxygen Graphics |

Renderer Core must not own GBuffer/shadow/environment policy. `SceneRenderer`
must not own surface presentation. Stages/services must not create alternate
per-view publication stacks. Numeric `ViewId` ownership is not temporal-state
ownership; service histories must be keyed by `ViewStateHandle` when history is
enabled.

## 7. Frame Lifecycle

### 7.1 High-Level Flow

```text
FrameStart
  - retire completed transient leases for the frame slot/fence
  - reset renderer publication state for the new frame
  - begin SceneRenderer frame

PreRender
  - sync active CompositionViews
  - publish/update runtime ViewContexts
  - build FrameViewPackets
  - split Primary, Auxiliary, and CompositionOnly packets
  - build ViewFamilies / RenderBatches
  - invoke view extensions: OnFamilyAssembled / OnViewSetup
  - build CompositionPlan skeletons for requested surfaces

Render
  - for each render batch:
      - build or reuse frame-shared scene inputs
      - run InitViews once for the batch to publish per-view prepared frames
      - run family-global stages once when their products are view-shared
      - for each auxiliary dependency, then each primary view packet:
          - acquire scene-texture lease
          - enter PerViewScope for the packet
          - publish pre-scene view bindings/constants
          - invoke OnPreRenderView_GPU hooks
          - run per-view scene stages
          - run post-process/tone map to the view output
          - record per-view overlay batches
          - invoke OnPostRenderView_GPU hooks
          - publish final view products
          - extract auxiliary or composition products required after lease release

Compositing
  - execute per-view screen overlays that target view outputs
  - execute surface composition plans in deterministic order
  - execute surface-level overlays/tools
  - mark surfaces presentable when their submission completed

FrameEnd
  - finalize extracts/histories
  - release or pin resources according to GPU fences and history ownership
```

### 7.2 Current-to-Target Transition

The existing single-cursor render path remains a valid special case:

```text
one scene view + one surface + one full-screen opaque layer
```

M06A must generalize the outer loop so the same code path handles:

```text
four scene views + one surface grid
two scene views + two surfaces
one scene view + one offscreen texture + one surface
primary view + PiP overlay
```

No separate PiP path is allowed.

### 7.3 View Extension Hooks

Renderer Core invokes typed extension hooks during family render, mirroring the
shape of UE's `ISceneViewExtension` without copying the API:

| Hook | Timing | Thread/queue |
| --- | --- | --- |
| `OnFamilyAssembled` | after batch construction, before GPU work | CPU |
| `OnViewSetup` | per view, after packet materialization, before scene stages | CPU |
| `OnPreRenderViewGpu` | after per-view bindings are published, before first scene pass | graphics queue in M06A |
| `OnPostRenderViewGpu` | after post-process and view overlays, before lease release/composition | graphics queue in M06A |
| `OnPostComposition` | after one surface composition completes | graphics queue in M06A |

Hooks are typed. They may submit overlay batches, diagnostics, or view-local
resource requests through declared payloads. Untyped strings, global late
callbacks, and direct stage mutation are not allowed extension mechanisms. The
existing `CompositionView::on_overlay` callback becomes a compatibility shim
that produces a `View screen overlay` batch.

## 8. Stage Scheduling Contract

### 8.1 Family-Global Work

The following work may execute once per render batch or once per scene/frame
when inputs are compatible:

| Stage / service | Sharing rule |
| --- | --- |
| Frame scene traversal | Once per scene/frame, then cached candidate refinement per view. This preserves the `InitViews` traversal contract. |
| Stage 6 light data | Frame light set is shared. Per-view clustered/tiled products are keyed per view when needed. |
| Stage 8 shadow depths | Directional, spot, and point shadow maps are light/scene products. They should be reused across views unless a view-specific show flag or quality override requires separation. |
| Environment LUTs | Sky/atmosphere LUTs that are physically shared by scene/time are shared. Aerial perspective and view-depth dependent products are per view. |
| Static GPU resources | Geometry, material constants, textures, and transform publications are shared by bindless index. |

### 8.2 Per-View Work

The following products are per view unless a later design proves a safe
multi-view representation:

- `PreparedSceneFrame` draw metadata and per-view visibility
- `ViewConstants`
- `SceneTextureBindings`
- scene depth, GBuffers, scene color, velocity
- HZB and occlusion results
- exposure state through `ViewStateHandle`; sharing reads a source handle's
  previous-frame exposure product, not a current-frame ordering dependency
- temporal history and previous-view matrices
- post-process output
- debug visualization output
- overlay command batches and hit-test metadata

### 8.3 Shading Mode and Capability Gating

`ShadingMode` is per view. The effective mode lives in `ViewRenderPlan`, not in
frame-global state:

- deferred views run base pass into GBuffers plus emissive/auxiliary scene color,
  then run deferred lighting
- forward views route base pass lighting directly into scene color and skip
  deferred lighting for that view
- mixed deferred/forward frames are valid only when every affected stage reads
  the selected view plan and publishes view-keyed products
- if a mode changes attachment requirements or stage topology beyond what one
  batch can safely express, the batch splitter separates those views

Services are capability-gated per view through typed feature/show masks. When a
service is disabled for a view, it must publish an explicit empty product or
typed absence for that `ViewId`; consuming stages must handle that view as a
null-safe no-op. A disabled view must never accidentally consume another view's
"last published" shadows, lighting, fog, debug state, or scene-texture bindings.

Shared service work uses the union of active view requirements to build
frame-wide data, then publishes per-view filtered bindings. For example, one
view may disable shadows while another view uses the shared shadow atlas; the
shadow service still builds the atlas for the enabled view and publishes an
empty shadow binding for the disabled view.

Vortex deliberately diverges from UE show-flag placement. UE keeps
`FEngineShowFlags` on the family and typically renders editor viewports through
separate families. Vortex allows per-view feature masks inside one render
batch. Consequence: shared products whose existence depends on a feature are
built from the union of active requirements, but every consumer reads the
per-view binding subset.

Affected shared or optionally shared products:

| Product / feature | Shared build rule | Per-view publication when disabled |
| --- | --- | --- |
| shadow maps | build for enabled views/lights once per compatible light set | empty shadow frame binding |
| light grid / clustered data | build union light set; view-local culling data stays per view | empty or reduced lighting binding |
| environment LUTs | build scene/time-shared LUTs when any view needs them | environment binding with disabled flags |
| aerial perspective / volumetric fog | per view unless a later design proves a safe packed representation | invalid product slots and disabled flags |
| custom depth/stencil | allocate/build if any view needs it | invalid custom-depth/stencil bindings |
| velocity | allocate if any history-capable view needs it | invalid velocity binding or zero validity flag |
| editor primitives | allocate editor primitive targets if any view requests them | empty overlay/editor-primitive batches |
| bloom/DOF/distortion/TAA | build per view for M06A; future shared products must add explicit per-view bindings | disabled post-process feature bits |

Render/debug mode classification:

| Mode class | Examples | Batch rule |
| --- | --- | --- |
| `kSplitsBatch` | `Wireframe`, `ShaderComplexity`, `MeshUVDensity`, MSAA editor primitives, modes requiring different attachment topology | separate batch or sub-batch |
| `kPermutationOnly` | `ForceUnlit`, `BaseColor`, `WorldNormals`, `Roughness`, `Metalness`, `MaterialAO`, `DirectionalShadowMask`, scene-depth visualizers | same batch allowed; stage selects per-view permutation |
| `kOverlayPass` | `OverlayWireframe`, view/surface screen overlays | same batch allowed if base scene topology is unchanged |

Any new debug visualizer must declare one of these classifications before it is
accepted. Unknown visualizers split the batch by default.

### 8.4 Per-View Stage Table

Stage numbers refer to the Vortex stage ordering defined in
`design/vortex/ARCHITECTURE.md` and `SceneRenderer::GetAuthoredStageOrder()`.

| Stage | Execution |
| --- | --- |
| 2 InitViews | Once per render batch; publishes per-view prepared frames. |
| 3 DepthPrepass | Per scene view. |
| 5 HZB/Occlusion | Per scene view. |
| 6 Forward light data | Shared frame light set; per-view published lighting bindings. |
| 8 Shadow depths | Shared shadow maps where compatible; per-view shadow bindings. |
| 9 BasePass | Per scene view. |
| 10 SceneTextures publish | Per scene view. |
| 12 Deferred lighting | Per scene view. |
| 14/15 Environment | Split shared LUT/update work from per-view composition. |
| 18 Translucency | Per scene view. |
| 21 Resolve | Per scene view. |
| 22 PostProcess | Per scene view. |
| 23 Cleanup/extract | Per scene view for products; frame-level retirement remains one pass. |

## 9. Scene Texture Leases and Resource Lifetime

The current LLD's "Option A: reallocate SceneTextures per view" is rejected.
It causes allocator churn and does not scale to editor layouts. M06A must use
descriptor-keyed leases from a scene-texture pool.

### 9.1 SceneTextureLease

A scene-texture lease is an exclusive runtime handle to a concrete
`SceneTextures` family for one active view render. Its key includes:

- extent
- render resolution scale. M06A locks this to 1.0; dynamic resolution is
  deferred.
- color format
- depth/stencil format
- GBuffer layout/count
- velocity/custom-depth requirements
- sample count
- editor primitive sample count and editor primitive attachment requirement
- reverse-Z/depth convention if encoded into resource usage
- HDR/SDR output requirement
- debug visualization attachment requirements
- queue affinity. M06A leases are `GraphicsOnly`; future async compute must
  extend the key or handoff metadata before sharing the same pool.

The lease owns no history. It is transient. It is released when all consumers of
that view's live scene products have finished or when explicit extracted
artifacts have been created for later consumers.

### 9.2 Pooling Rules

1. A lease cannot be reused while any command list in the current frame can
   still write or read it.
2. A lease can be reused for another view later in the same frame only after
   the first view has extracted/published every product needed by composition,
   diagnostics, and history.
3. A lease can remain pinned across frames only if it is explicitly owned as
   history. SceneTextures themselves are not history.
4. A view output that composition will sample must be a stable composite-source
   texture, not a live mutable GBuffer attachment.
5. Descriptor views are cached by resource + view description. Reallocating a
   scene-texture family invalidates only the descriptors owned by that family.
6. Two simultaneously live views with the same descriptor key must receive two
   different leases unless the renderer serializes them and proves the first
   lease has no remaining readers/writers.
7. Pool exhaustion is an explicit validation failure in M06A. It must not
   silently fall back to unbounded allocation during steady-state editor-grid
   rendering.

### 9.3 Persistent Per-View Resources

Per `ViewStateHandle`, Vortex may retain:

- exposure history
- previous view matrices and temporal history
- previous HZB where required
- persistent HDR/SDR intermediate output when the view intentionally keeps a
  stable composite source
- overlay/hit-test state needed by tools

Retained resources must never be keyed only by z-order or by surface index. A
service may use `ViewId` as a publication key for the current frame, but its
history owner is `ViewStateHandle`.

History invalidation matrix:

| State | Invalidate when |
| --- | --- |
| exposure | handle changes, owner requests reset, exposure config changes incompatibly, source handle inactive without explicit stale-use opt-in |
| previous view matrices | handle changes, scene changes, projection/viewport descriptor changes beyond jitter, owner reset |
| previous HZB / occlusion | handle changes, extent/depth format/sample count changes, occlusion mode changes |
| TAA/post-process history | handle changes, render resolution scale changes, tone-map/history format changes, debug mode bypasses history |
| persistent view output | descriptor changes, surface route drops the persistent output, owner releases handle |

The existing `PreviousViewHistoryCache` keyed only by `ViewId` is a transition
artifact. Slice 1 must either route it through `ViewStateHandle` or make it
stateless for views without a handle.

### 9.4 Shared Frame Resources

These are shared across view packets in a compatible batch:

- uploaded transforms, previous transforms, normals, skinning, morph, material
  WPO, motion-vector status
- static geometry/material resources
- frame light selection
- shadow atlas/surfaces for camera-independent shadows
- bindless descriptor tables and root bindings

Per-view culling changes draw metadata, not the identity or upload count of
underlying world/material resources.

## 10. Resource State Contract

All resources must have an explicit producer/consumer state handoff. State names
below are Oxygen Graphics `ResourceStates`, not raw D3D12 enum names. No stage
may rely on the last view's final state as an implicit global contract.

| Resource | Producer state | Consumer state | Final handoff |
| --- | --- | --- | --- |
| SceneDepth | `DepthWrite` during depth/base stages | `DepthRead` or `ShaderResource` for HZB, lighting, post, overlays | per-view lease returns to pool only after final consumer |
| GBuffers | `RenderTarget` in base pass | `ShaderResource` for deferred lighting/debug/post | per-view lease scoped |
| SceneColor | `RenderTarget`/`UnorderedAccess` during lighting/environment/translucency | `ShaderResource` or copy source for resolve/post/debug | per-view resolve/post output |
| View SDR/HDR output | `RenderTarget` during tone map/overlay | `ShaderResource` or copy source in composition | surface composition read |
| Surface backbuffer | `Present`/known incoming | `RenderTarget` or copy dest in composition | `Present` after composition |
| Offscreen surface texture | `RenderTarget` or copy dest during composition | `ShaderResource` for next-frame or same-frame declared consumers | pinned by surface frame slot/fence until consumers retire |
| Cross-queue handoff | graphics queue only in M06A | not applicable until async compute is added | lease retirement waits on the latest producing queue fence |

Renderer Core owns composition resource tracking. `SceneRenderer` owns
scene-texture state transitions inside the scene stage chain. A stage that
publishes a product must leave it in the state declared by that product's
contract or record a transition before the next consumer.

M06A rendering and composition are graphics-queue only. Compute dispatches that
exist today also record on the graphics queue. If HZB, shadows, volumetric fog,
or post-processing moves to async compute later, the lease must track
per-queue ownership and wait on the latest producer/consumer fence for every
queue before reuse or release.

## 11. Render Settings and Debug Modes

UE editor parity requires view-local render settings. Vortex must not keep
`RenderMode` and `ShaderDebugMode` as effective frame-global values once M06A
multi-view execution is active.

### 11.1 ViewRenderSettings

M06A should introduce or equivalent-store:

```cpp
struct ViewRenderSettings {
  RenderMode render_mode { RenderMode::kSolid };
  ShaderDebugMode shader_debug_mode { ShaderDebugMode::kDisabled };
  DepthPrePassMode depth_prepass_mode { DepthPrePassMode::kOpaqueAndMasked };
  graphics::Color wire_color { 1.0F, 1.0F, 1.0F, 1.0F };
  float render_resolution_scale { 1.0F };
  bool gpu_debug_pass_enabled { true };
};
```

`FramePlanBuilder` may apply frame defaults, but the final `ViewRenderPlan`
must be per view. The following must be independently testable per view:

- wireframe does not force another view to wireframe
- base-color/normals/depth debug mode does not leak to another view
- shadow-mask debug does not disable lighting in another view
- neutral tonemap is applied only to views that request a debug mode requiring
  it

M06A pins `render_resolution_scale` to 1.0. A view's scene-texture extent is
computed from its render viewport, not from the destination surface rectangle.
Post-process resolves or copies the view output into the surface destination
rect. Dynamic resolution, screen percentage, TSR, and per-view upscalers are
deferred, but their future insertion point is the per-view post-process/resolve
step rather than surface composition.

### 11.2 Exposure Sharing

Exposure sharing follows UE's view-state model, not the current Oxygen
source-before-consumer ordering:

- each history-capable view owns exposure state through `ViewStateHandle`
- a view that specifies an exposure source reads the source handle's
  previous-frame exposure product at frame start
- the source view's current frame may update its own exposure state for the
  next frame, but consumers do not wait for it in the same frame
- if the source handle is missing or inactive in the current frame, the
  consumer uses its own previous exposure if valid, otherwise fixed exposure,
  and diagnostics must record the fallback
- current-frame exposure dependencies are out of M06A scope and require a
  follow-up design with explicit dependency edges

This removes the need for z-order or render-order constraints on exposure
sharing while preventing stale hidden bindings. If a future feature needs
current-frame exposure transfer, it must use the same auxiliary dependency graph
defined in sections 5.4 and 5.6 rather than reintroducing ad hoc view-order
validation.

### 11.3 Show-Flag Analogue

UE uses `FEngineShowFlags` per view family/client. Vortex does not need to
clone UE's full flag set in M06A, but it needs a typed feature mask that can
express editor-relevant controls without cross-domain payload leakage:

- M06A classes: scene meshes, ground grid, direct lighting, sky/atmosphere/fog
  enablement, local lights, shadows, debug visualization, screen/surface
  overlays
- Deferred classes: full UE show-flag coverage, collision view, full custom
  depth/stencil editor workflows, distortion, depth of field, motion blur,
  selection outline rendering, all editor gizmo primitive types

Flags must be schema-first and typed. No stringly typed "mode" payload should
flow into stage modules.

## 12. Overlay Architecture

The existing `CompositionView::on_overlay` callback is only a late SDR command
hook. It is not enough for editor parity. M06A must define overlay lanes that
map to UE's PDI/canvas separation without copying UE APIs directly.

### 12.1 Overlay Lanes

| Lane | Depth policy | Examples | Target |
| --- | --- | --- | --- |
| World depth-aware overlay | depth-tested or depth-biased | gizmos, frustums, debug lines, selection bounds, light icons with occlusion | view scene output before or after tone map according to material/color policy |
| World foreground overlay | no depth or foreground depth priority | transform widget handles, always-visible axes | view output after opaque/translucent scene |
| View screen overlay | screen-space | stats, rulers, labels, viewport badges | view SDR output after tone map |
| Surface overlay | screen-space over final layout | tools UI, ImGui, global stats | surface backbuffer after view composition |

### 12.2 Overlay Batches

Overlay producers submit typed batches keyed by `ViewId` or surface id. A batch
contains primitives or a callback plus:

- lane
- z/depth priority
- color-space expectation
- target view/surface
- stable debug name derived from `(ViewId, lane, batch_index, surface_id)`
- optional hit-test id range

Scene stages consume world overlay batches at the correct point in the
per-view pipeline. Renderer Core consumes surface overlay batches after
surface composition. This prevents a global ImGui/tools callback from becoming
the only overlay mechanism.

### 12.3 Editor Primitive Targets

UE renders editor primitives through a dedicated path with world/foreground
depth-priority groups and optional MSAA editor-primitive targets. Vortex must
reserve the same low-level shape:

| Concept | Vortex contract |
| --- | --- |
| world editor primitives | depth-tested world overlay lane before view flattening |
| foreground editor primitives | foreground overlay lane after world depth-aware overlay |
| editor primitive color/depth | optional attachments in the scene-texture lease key |
| editor primitive sample count | `editor_primitive_sample_count` in the descriptor key |
| composite into scene color | explicit per-view overlay composite pass before view output extraction |

M06A must define the attachment, lane, and composite contracts. It does not need
to implement a full editor primitive mesh processor or every gizmo primitive.
If MSAA editor primitive rasterization is deferred, that deferral is explicit:
future implementation must use the reserved target policy and must not invent a
separate overlay renderer.

## 13. Composition Architecture

### 13.1 Layer Types

The composition pass supports:

- copy layer: source covers destination, opacity 1, compatible formats
- blend texture layer: alpha blend a source texture into destination
- color-space conversion layer: HDR/linear/sRGB handling when source and
  destination differ
- future upsample/resolve layer: dynamic-resolution or MSAA resolve

The fast full-coverage copy optimization is valid only when:

- one opaque source layer covers the full destination viewport
- no prior layer on the target needs preservation
- formats and color spaces are compatible or a required conversion is explicit
- no surface overlay has to be inserted before that copy

The predicate is purely structural and belongs to the `SurfaceCompositionPlan`.
It must not test for a "primary" view, `kZOrderScene`, or a hardcoded view
identity.

### 13.2 Multi-Surface Routing

One frame can produce:

```text
Surface A:
  View 1 full rect
  View 2 top-right inset
  Surface overlay

Surface B:
  View 3 full rect

Offscreen Surface C:
  View 1 copied for texture consumer
```

The composition planner must not assume a highest z-order view is the implicit
default surface layer. It builds per-surface plans from routing metadata.

### 13.3 Color and Depth Semantics

- Scene views render in HDR when they run the scene pipeline.
- View screen overlays render after view tone map unless a lane explicitly
  requests HDR composition.
- Surface overlays render in the surface color space.
- Depth is not shared across independently rendered views for composition.
  Depth-aware overlays must target their own view's depth before the view is
  flattened for composition.
- Offscreen surfaces that are consumed as textures are not presentable
  backbuffers. Their final handoff state is `ShaderResource`, and their lifetime
  is pinned by the surface frame slot/fence until declared consumers retire.

## 14. Interface Deltas

This section defines expected implementation changes. Names may evolve during
implementation, but the ownership and data flow must remain.

### 14.1 `CompositionView`

Required additions or equivalent adjacent type:

- `ViewRenderSettings render_settings`
- `ViewKind view_kind`
- `ViewStateHandle view_state`
- `std::vector<ViewSurfaceRoute> surface_routes`
- `OverlayPolicy overlay_policy`
- typed feature/show mask
- `std::vector<AuxOutputDesc> produced_aux_outputs`
- `std::vector<AuxInputDesc> consumed_aux_outputs`

Existing fields remain meaningful:

- `id` is still the stable intent id
- `z_order` remains a layer ordering input, not a surface identity
- `exposure_source_view_id` remains an intent-level convenience field, but M06A
  resolves it to a source `ViewStateHandle` and previous-frame exposure product
  without current-frame order validation
- `shading_mode` remains per view

Auxiliary route examples:

```cpp
CompositionView reflection = CompositionView::ForScene(...);
reflection.view_kind = ViewKind::kAuxiliary;
reflection.produced_aux_outputs = { AuxOutputDesc {
  .id = AuxOutputId { 17 },
  .kind = AuxOutputKind::kColorTexture,
  .debug_name = "PlanarReflection.Main",
} };

CompositionView main = CompositionView::ForScene(...);
main.consumed_aux_outputs = { AuxInputDesc {
  .id = AuxOutputId { 17 },
  .required = true,
} };
```

The route can still copy the auxiliary output into an offscreen surface. That is
ordinary surface routing, not a second auxiliary-product mechanism.

### 14.2 `FramePlanBuilder`

Change from frame-global effective mode to per-view effective mode:

- build `ViewRenderPlan` from frame defaults plus view-local settings
- record render/debug mode in `FrameViewPacket`
- record `ViewKind`, `ViewStateHandle`, feature mask, route list, and scene
  texture descriptor key in `FrameViewPacket`
- keep sky/environment plan per view
- remove `frame_render_mode_` and `frame_shader_debug_mode_` as stage-readable
  state in slice 1. Their values may be consumed only by
  `FramePlanBuilder::DefaultViewRenderPlan(...)` to seed a packet that did not
  provide overrides.
- after slice 1, stages must read `ViewRenderPlan` / `PerViewScope` exclusively.
  A grep/static gate must fail implementation if production stage code reads
  removed frame-global symbols as the effective mode.

### 14.3 `RenderContext`

Add helper semantics, not a second context type:

- select active view by entering a typed `PerViewScope`
- bind a `SceneTextureLease` or view-local scene-texture family for the active
  view
- expose current view packet/render plan
- preserve single-pass harness materialization for tests

`current_view` remains a transition-facing cursor for existing stage APIs, but
it is no longer free mutable state. Only `PerViewScope` may set it:

```cpp
class PerViewScope {
public:
  PerViewScope(RenderContext& ctx, const FrameViewPacket& packet,
    SceneTextureLease& lease);
  ~PerViewScope();
};
```

The scope captures the selected packet and asserts in debug that `current_view`,
`active_view_index`, view constants, view frame bindings, scene-texture
bindings, and lease identity still match on exit. Stages must not cache
`ctx.current_view` outside the scope. Future parallel view recording must use a
per-view context clone or explicit `FrameViewPacket&`; it must not share one
mutable cursor across workers.

Nested `PerViewScope` on the same `RenderContext` is a programming error and
must `CHECK`/`DCHECK` in debug. Extension hooks that need a sub-view must
request an auxiliary packet/dependency before rendering starts; they must not
re-enter scene rendering by opening another scope inside the active one.

### 14.4 `SceneRenderer`

Target contract:

```cpp
void SceneRenderer::RenderViewFamily(RenderContext& ctx,
  const ViewRenderBatch& batch);

void SceneRenderer::RenderView(RenderContext& ctx,
  const FrameViewPacket& packet,
  SceneTextureLease& scene_textures);
```

`RenderViewFamily` iterates `batch.all_scene_views`, not only surface-routed
views. Auxiliary dependencies render before primary consumers. The current
`OnRender(RenderContext&)` can delegate to this path with a batch of one. It
must not remain the only production multi-view path.

### 14.5 `SceneTextures`

Slice D introduced a Vortex-native pool/lease owner. The concrete
`SceneTextures` class remains the product family, while
`SceneRenderer::RenderViewFamily` acquires descriptor-keyed leases for scene
views and keeps the original `SceneTextures scene_textures_` member as the
single-current-view fallback/inspection bootstrap rather than the only
production store.

Required behavior:

- acquire by descriptor key
- reset setup/bindings per lease
- publish `SceneTextureBindings` per view
- extract required composition/history artifacts before lease release
- retire by frame slot/fence and queue-affinity metadata
- expose editor primitive attachment descriptors even if their raster producer
  is deferred

### 14.6 Schema-First Payload Validation

`CompositionView`, `ViewRenderSettings`, `ViewSurfaceRoute`, `OverlayPolicy`,
feature/show masks, and auxiliary IO descriptors are C++ runtime API types.
Their first line of validation is the type system plus constructor/build-time
checks in Renderer Core.

Any serialized payload that creates or mutates these types must be
schema-validated before it reaches Vortex:

- validation-scene and proof-run view layout payloads: add
  `tools/vortex/schemas/multiview-validation.schema.json`
- reusable scene/importer payloads: extend the owning scene/import schema, not
  Vortex stage code
- no stringly typed render mode, feature flag, overlay lane, or auxiliary
  product id may cross into stage modules

If a slice uses only direct C++ test construction, that slice is classified as
non-serialized runtime API validation and must cover invalid values with unit
tests. The first slice that adds JSON/demo-settings authoring for multi-view
layouts must add the schema in the same commit.

## 15. Algorithms

### 15.1 Build View Batches

```cpp
for (CompositionViewImpl* view : ordered_active_views) {
  auto packet = BuildFrameViewPacket(view);
  if (packet.kind == ViewKind::CompositionOnly) {
    composition_only_packets.push_back(packet);
    continue;
  }

  auto key = BuildViewFamilyKey(packet);
  if (packet.kind == ViewKind::Auxiliary) {
    batches[key].auxiliary_views.push_back(packet);
  } else {
    batches[key].surface_views.push_back(packet);
  }
}

for (ViewRenderBatch& batch : batches) {
  batch.all_scene_views = TopologicalSort(
    batch.auxiliary_views, batch.surface_views, batch.dependencies);
}

ViewBatchGraph batch_graph = BuildViewBatchGraph(batches, aux_edges);
ordered_batches = TopologicalSort(batch_graph);
```

`aux_edges` are built by resolving every `AuxInputDesc.id` to the unique
`AuxOutputDesc.id` producer. Duplicate required producers or unresolved
required inputs fail validation before rendering.

Batching is about shared render work and resource compatibility. Final surface
composition order is handled later by the composition plan.

### 15.2 Render a Batch

```cpp
for (ViewRenderBatch& batch : ordered_batches) {
  SelectBatch(ctx, batch);
  view_extensions.OnFamilyAssembled(batch);
  init_views.Execute(ctx, batch.all_scene_views);
  BuildSharedLightingAndShadows(batch);  // union/shared where compatible

  for (FrameViewPacket& packet : batch.all_scene_views) {
    view_extensions.OnViewSetup(packet);
    auto lease = scene_texture_pool.Acquire(packet.SceneTextureKey());
    PerViewScope view_scope(ctx, packet, lease);

    PublishPreSceneViewBindings(ctx);
    view_extensions.OnPreRenderViewGpu(ctx, packet);
    RenderViewStages(ctx, lease.SceneTextures());
    view_extensions.OnPostRenderViewGpu(ctx, packet);
    PublishPostSceneViewBindings(ctx);
    ExtractViewOutput(ctx, lease);
    scene_texture_pool.ReleaseWhenSafe(std::move(lease));
  }
}
```

### 15.3 Compose Surfaces

```cpp
for (SurfaceCompositionPlan& surface_plan : plans) {
  TrackSurfaceTarget(surface_plan.target);
  ApplyClearPolicy(surface_plan);

  for (CompositionLayer& layer : surface_plan.layers) {
    TrackLayerSource(layer);
    ExecuteCopyOrBlend(layer, surface_plan.target);
  }

  ExecuteSurfaceOverlays(surface_plan.surface_id);
  MarkPresentable(surface_plan.surface_id);
}
```

## 16. Diagnostics and Capture Contract

Every diagnostics record produced during M06A must identify:

- frame sequence and frame slot
- published `ViewId`
- intent `ViewId` when available
- view name
- render batch id
- surface id for composition work
- stage/service name
- pass/product name
- deterministic debug name

RenderDoc event labels must include view or surface identity for repeated work:

```text
Vortex.View[MainPerspective].Stage9.BasePass
Vortex.View[TopWire].Stage9.BasePass
Vortex.Surface[MainWindow].Composite.Layer[MainPerspective]
```

Debug names are a deterministic function of stable ids:

| Record | Name shape |
| --- | --- |
| scene pass | `Vortex.View[{view_name}:{view_id}].{stage}.{pass}` |
| overlay batch | `Vortex.View[{view_name}:{view_id}].Overlay[{lane}:{batch_index}]` |
| composition layer | `Vortex.Surface[{surface_id}].Composite.Layer[{layer_index}:{source_view_id}]` |
| auxiliary output | `Vortex.AuxView[{view_name}:{view_id}].Extract[{product}]` |

If intent provides no non-empty view name, diagnostics use `"Unnamed"` as the
view-name token. Empty-name labels such as `Vortex.View[:42]` are invalid.

"Last view wins" means a product slot, diagnostic record, debug label, or
binding field is overwritten by whichever view rendered most recently, losing
the earlier view's identity. No product record may conflate all views this way.
If a product is per view, the diagnostic key is per view.

M06A validation tooling must add:

- `tools/vortex/AnalyzeRenderDocVortexMultiView.py`
- `tools/vortex/Run-VortexMultiViewValidation.ps1`
- `tools/vortex/Assert-VortexMultiViewProof.ps1`
- `tools/vortex/multiview_cdb_allow.json`

The wrapper must reuse `tools/vortex/VortexProofCommon.ps1` and the shared
RenderDoc UI lock through `tools/shadows/Invoke-RenderDocUiAnalysis.ps1`. The
analysis report must be key/value shaped and gated with
`Assert-VortexProofReportStatus`.

## 17. Validation Plan

### 17.1 Unit Tests

1. Four active scene views produce four `FrameViewPacket`s with independent
   render/debug modes.
2. `FramePlanBuilder` does not leak wireframe/debug mode between views.
3. `ViewLifecycleService` resolves exposure sharing through previous-frame
   `ViewStateHandle` state and does not impose current-frame render order.
4. Exposure source view disabled/inactive in the current frame uses the
   documented fallback and records a diagnostic, not a stale hidden binding.
5. History is not reused after a producer drops a `ViewStateHandle` and
   recreates a view with the same `ViewId`.
6. Descriptor-key changes for resize, format, sample count, or
   history-incompatible render-mode changes invalidate persistent history.
7. `InitViewsModule` publishes separate `PreparedSceneFrame` payloads for two
   cameras over the same scene.
8. Mixed deferred/forward views do not leak GBuffer/deferred-lighting state
   across view boundaries.
9. Capability-disabled services publish explicit empty per-view products, and
   enabled views still receive valid products.
10. Shared union build / per-view consume: view A enables shadows, view B
    disables shadows, the shadow service builds the union atlas, view A receives
    a non-empty binding that reads it, view B receives the typed empty shadow
    product, and disabling B does not change A's atlas content.
11. `SceneTextureLease` pool rejects active aliasing and reuses only after
   release/retirement.
12. Two views with the same scene-texture key receive distinct leases unless
    explicitly serialized by `PerViewScope`.
13. Pool exhaustion fails with a diagnostic instead of unbounded steady-state
    allocation.
14. `SceneTextureBindings` are published per view and do not get overwritten by
    another view before composition consumes them.
15. Composition planning routes one view to two surfaces and two views to one
   surface with deterministic ordering.
16. Overlay batches are lane-sorted and scoped to the correct view/surface.
17. Auxiliary view packets render before primary consumers and extract their
    products before lease release.
18. Cross-batch auxiliary dependency edges topologically order producer batches
    before consumer batches.
19. Nested `PerViewScope` on the same `RenderContext` fails in debug.
20. Serialized multi-view validation payloads reject invalid render modes,
    feature flags, overlay lanes, surface routes, and auxiliary IO descriptors
    through schema validation before C++ conversion.

### 17.2 Integration Tests

1. One surface, four viewports: lit perspective, wireframe top, debug-normal
   side, shadow-mask camera. Verify independent outputs and no setting leakage.
2. Mixed deferred/forward frame: one deferred lit view and one forward lit view.
   Verify stage routing, scene products, and final composition independently.
3. Capability override frame: shadows disabled in one view and enabled in
   another. Verify explicit empty shadow publication for the disabled view.
4. Two surfaces: primary surface with one lit view, secondary surface with a
   different camera. Verify both surfaces receive expected view outputs.
5. PiP as ordinary composition: primary view full surface plus secondary view
   sub-rect. Verify no special PiP path exists.
6. Exposure sharing: secondary view explicitly consumes primary previous-frame
   exposure. Verify render order does not matter.
7. Scene-capture-style auxiliary view writes an extracted texture consumed by a
   primary view in the same frame.
8. Composition-only UI/overlay view: no scene passes run, but the layer
   composes after scene views.
9. Resize during an editor-grid run invalidates histories and reacquires leases
   without bleeding old products into new extents.
10. Surface present/submission failure retires or preserves transient leases by
   fence ownership; no leaked active lease remains.

### 17.3 Runtime Validation

For M06A closure, validation requires:

- successful test binary run for the M06A unit/integration set
- CDB/debug-layer run of the validation scene with zero D3D12/DXGI validation
  messages of severity `WARNING` or higher, except entries explicitly
  allow-listed in `tools/vortex/multiview_cdb_allow.json`, and no debugger
  break. The report must explicitly fail mismatched resource-state transitions,
  untracked transitions, present-without-presentable-state, descriptor lifetime
  errors, and live-object leaks not covered by the allow-list.
- RenderDoc capture and scripted analysis proving:
  - repeated per-view stage labels are present
  - at least four view outputs exist in the capture for the editor-grid test
  - per-view debug/render modes differ as requested
  - final surface composition consumes the expected view outputs in order
  - auxiliary view outputs are extracted before primary consumers sample them
  - no unexpected scene-texture reallocation storm occurs during a steady frame
- steady-state allocation check: in a 60-frame editor-grid integration run,
  scene-texture family allocations after a 5-frame warmup must be 0. If a new
  descriptor key appears after warmup, the allowed additional allocation count
  is `<= count(new distinct descriptor keys)`. Any allocation above that bound
  fails validation.

Manual visual inspection can approve image quality, but it cannot replace the
capture/script evidence.

## 18. M06A Implementation Slices

M06A should land in narrow slices. Do not implement a full editor UI, a new RDG,
parallel rendering, or a global show-flag clone unless the LLD is updated first.

1. **Per-view plan and state handles:** move effective render/debug mode into
   `FrameViewPacket`/`ViewRenderPlan`, introduce `ViewKind` and
   `ViewStateHandle`, remove stage-readable frame-global render/debug state,
   and add tests for no leakage/history reuse.
2. **Renderer Core view-family loop with serialized scene textures:** render
   every eligible `Primary`/`Auxiliary` scene view in a batch through
   `PerViewScope`, using the existing single `SceneTextures` family
   serially. This slice proves the loop and product isolation before adding a
   pool. Slice 2 alone does not satisfy acceptance gate item 4. Implemented by
   slice C.
3. **Scene-texture leases:** introduce descriptor-keyed lease/pool and route
   the multi-view loop through it. The lease pool's alias/fence/concurrency
   contract is meaningful only after slice 2 exercises more than one view.
   Implemented by slice D with a focused warmup allocation metric; the full
   60-frame runtime churn report remains a slice-H/closure artifact.
4. **Composition plan generalization:** compose arbitrary view outputs into one
   or more surfaces; represent PiP as data and replace primary/z-order copy
   assumptions with structural layer predicates. The predicate must already
   accept auxiliary sources and offscreen-surface destinations. Implemented by
   slice E with route-aware layer plans, structural full-surface copy
   selection, deterministic surface/layer debug names, and filtered surface
   submissions. Full auxiliary producer/consumer plumbing remains slice F.
5. **Auxiliary view dependencies:** add extracted auxiliary outputs and at
   least one scene-capture-style validation producer/consumer. This slice owns
   auxiliary product extraction and material/stage consumption, not the basic
   ability for a surface plan to route an auxiliary source. Slice F implements
   the typed dependency graph, producer validation, optional typed-invalid
   bindings, and topological packet ordering. Runtime validation now proves a
   stage-level same-frame color product consumer with an extracted producer
   texture copied into the dependent view under a RenderDoc-visible
   `Vortex.AuxView.Consume` GPU scope.
6. **Overlay lanes and view extensions:** add typed extension hooks, minimal
   view screen overlays, and surface overlays. Reserve editor primitive
   attachments/lane policy even if full MSAA primitive rasterization is
   deferred.
7. **Validation scripts:** add capture analysis for multi-view labels, output
   routing, auxiliary extraction, CDB allow-listing, and resource churn.

## 19. Non-Goals and Deferred Work

Not required for M06A:

- full editor application UI
- parallel per-view command recording
- multi-GPU fork/join
- XR/mobile multiview
- dynamic resolution per view
- complete UE-style show-flag coverage
- all editor gizmo primitives and the full editor primitive mesh processor
- MSAA editor primitive rasterization if the reserved target/composite contract
  is documented and tested with placeholder batches
- TSR/TAA implementation beyond preserving the state-handle and resolve
  insertion points

Deferred work must not be hard-blocked by M06A. The data model must leave room
for all of the above without another architecture rewrite.

## 20. Acceptance Gate

M06A can be closed only when all of the following are true:

1. The implementation renders and composes multiple scene views into one or
   more surfaces through the Vortex-native path.
2. Per-view render/debug settings are isolated.
3. Per-view scene products are keyed by view, and persistent histories are keyed
   by producer-owned `ViewStateHandle`; neither can overwrite another view.
4. Scene-texture allocation uses leases/pooling or an explicitly documented
   equivalent that avoids per-frame reallocation churn.
5. Composition is data-driven by surface plans, with PiP represented as a
   normal layer.
6. Auxiliary scene views are modeled and at least one same-frame auxiliary
   producer/consumer path is validated with runtime/capture proof.
7. Exposure sharing uses previous-frame view-state semantics or the LLD is
   updated with an approved non-UE divergence.
8. Required docs and implementation-status artifacts are updated.
9. Unit/integration tests, CDB/debug-layer run, and RenderDoc scripted analysis
   evidence are recorded.

Until those evidence items exist, M06A remains implementation-pending even if
this LLD is accepted.
