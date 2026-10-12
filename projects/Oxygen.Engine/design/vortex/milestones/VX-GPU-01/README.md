# VX-GPU-01 — CPU→GPU data contract integrity

Status: `in_progress`

| Field     | Summary                                                                                                                                  |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | GPU work reads only immutable, ordered, receipt-retired data; every unwired renderer feature is finished; ED-M08 resumes after closeout. |
| Remaining | Open: [VX-GPU-01](../../OPEN_ITEMS.md#p1--current-delivery), [VX-DIAG-01](../../OPEN_ITEMS.md#p1--current-delivery).                     |
| Evidence  | Per-slice evidence in the [slice table](#slices).                                                                                        |

This milestone runs before ED-M08 resumes. It closes the defect class behind
the BurgerPiz flicker and device removal (fixed in `09df1a6bd`): a pass read
draw and material slots through another pass's mutable inputs. A review of
every Vortex pass and shader against that class produced the
[findings](#findings) below; each was verified against the source. All
confirmed findings are in scope, whatever their severity. Clean code is a
requirement of this milestone, not a follow-up.

The designs own every contract; this plan owns order, files, tests, owner
actions and status. Paths are relative to `projects/Oxygen.Engine/src/Oxygen/`;
shaders are under `Graphics/Direct3D12/Shaders/Vortex/` (written `S/`).

Out of scope:

- Virtual shadow maps (`S/Services/Shadows/Vsm/*`, `VsmFrameBindings`,
  `virtual_shadow_frame_slot`) stay as they are: unfinished VSM work
  ([VX-SHADOW-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities)).
- Bloom, TAA/TSR, MSAA and the remaining post-processing family are scheduled
  after Editor V0.1: abdes/DroidNet#12, #15, #46 and #47.
- Skinned and morph rendering wait for the engine animation module
  (abdes/DroidNet#48,
  [VX-MOTION-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities)).

## Resume

After a context compaction or in a new session:

1. Find the first slice in the [slice table](#slices) that is not
   `validated`.
2. Open its section and continue at the first unchecked box.
3. Run `git status` and `git log --oneline -8` to see uncommitted and landed
   work.
4. Re-read only the design sections and findings the slice cites.

Update the slice table, the checkboxes and the header table as work lands.
Commit messages describe the change; they never cite this plan or its IDs.

## Target model

Every slice moves the renderer toward these rules. S0 writes them into the
owning designs; later slices enforce them.

1. **One immutable inputs record per pass invocation.** Allocated at record
   time; root constant `[1]` is its only entry point. It carries every slot and
   value the pass's shaders read.
2. **Nothing recorded work can reach is rewritten in place.** This covers view
   constants, shader-visible descriptors (`Replace`, `UpdateView`) and mapped
   buffers. Each content version gets a new descriptor or element.
3. **Lifetimes follow submission receipts.** A descriptor retires with its
   resource, in the same deferred action. Unregistering a shader-visible view
   immediately is an error outside shutdown.
4. **Producers and consumers are ordered.** Data written on another queue
   reaches a consumer only through a recorded completion dependency, or through
   a ticket the publisher waits on before publishing the index.
5. **Pure shader helpers.** Helpers take records or slots. Bound-view access
   lives in one header that only camera-view entry points may include.
6. **Fail loudly.** A failed publication is logged and rejects the view;
   required data never falls back to plausible defaults.
7. **Generated contracts.** C++ struct, HLSL struct, offset asserts and sampler
   slots come from one schema.
8. **No unwired code.** Every catalogued shader, published field and pass is
   consumed and tested, or removed.

## Decisions

Owner decisions, 2026-10-12:

| Decision                       | Value                                                                                                                        |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| Severity                       | Medium and low findings are in scope with the same weight as critical and high.                                              |
| VSM                            | Out of scope; never deleted as dead code.                                                                                    |
| Unwired features               | Conventional receiver analysis/mask and GPU debug primitives are finished here; skinned/morph wait for the animation module. |
| Bloom, TAA, MSAA, post-process | Logged as post-Editor-V0.1 features; not touched here.                                                                       |
| Sequence                       | This milestone completes before ED-M08 work resumes.                                                                         |

Design decisions, answered 2026-10-12; S0 records them in the owning designs:

| ID   | Decision                                                                                                                                                                                                                                     |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GD-1 | Material and procedural-grid uploads use the graphics-queue inline path, recorded before the frame's first pass. Queue order gives ordering; no cross-queue wait.                                                                            |
| GD-2 | Buffer growth creates a new buffer and descriptor, GPU-copies the old content on the graphics queue, then publishes the new index. Callers never re-upload.                                                                                  |
| GD-3 | Camera view constants stay as b1, versioned: each write is a new transient record bound by GPU address. Non-camera passes use their own inputs records (S3).                                                                                 |
| GD-4 | Skinned poses are deferred to the engine animation module (abdes/DroidNet#48).                                                                                                                                                               |
| GD-5 | Deformation execution (vertex shader or compute pre-pass) is decided with the animation module.                                                                                                                                              |
| GD-6 | Receiver analysis and receiver-mask caster culling both run GPU-driven: tight cascades from scene depth, shadow matrices read from a GPU buffer, CPU cascade culling kept as a loose bound.                                                  |
| GD-7 | Each texture content version (placeholder, loaded, error, evicted) gets its own descriptor; the old one retires by receipt. MaterialBinder re-serializes materials whose texture indices changed; per-frame bindings read the current index. |

## Working rules

- **Commits.** One thematic commit per slice once its tests pass; no review
  pause. A slice may land as more than one commit only when its parts build
  and pass independently. Work stops only at **Owner** boxes and for decisions I am not
  confident about, presented with facts and pros/cons.
- **GPU tests.** One GPU test process at a time, with `--gtest_filter` on the
  tests being worked on. A test that removes the D3D12 device is never rerun
  by me; the owner gets the command.
- **Builds.** I build engine targets and tests (Debug). The owner builds the
  SDK and the editor and runs the editor and RenderScene.
- **Before review.** `oxyformat` before building; `oxytidy` on changed C++
  before asking for review, zero warnings in those files.
- **Docs.** Update owner docs in the slice that changes the behavior they
  describe.
- **Deferrals.** Anything that cannot land in its slice becomes an
  abdes/DroidNet issue and an `OPEN_ITEMS` row in the same change.

Commands, from `projects/Oxygen.Engine` after `vcvars64`:

```text
cmake --build --preset oxygen-ninja-debug --target <targets>
ctest --preset oxygen-ninja-debug -R '^<test>$' --output-on-failure
./tools/cli/oxyrun.ps1 <test> -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=<case>
./tools/cli/oxyformat.ps1 <files> --fix
./tools/cli/oxytidy.ps1 <files> --configuration Debug --include-tests --build-dir out/build-ninja
python tools/vortex/CheckDocumentation.py
```

## Findings

Verified 2026-10-12. Verdict `PARTLY` states what holds.

| ID  | Finding                                                                                                                                                                                                        | Evidence                                                                                                                       | Verdict   | Slice    |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ | --------- | -------- |
| N1  | Material and procedural-grid constants upload on the transfer queue; tickets are discarded and no graphics list waits on them.                                                                                 | `Vortex/Resources/MaterialBinder.cpp:1437`; `Graphics/Common/CommandQueueSubmission.cpp:366-379`                               | CONFIRMED | S1       |
| N2  | TextureBinder's shared placeholder and error textures upload on the transfer queue and are published at once; no graphics list waits on them.                                                                  | `Vortex/Resources/TextureBinder.cpp` (`CreateFallback`); `InitViewsModule.cpp:572`                                             | CONFIRMED | S2       |
| C1  | Buffer growth recreates the SRV in the same live descriptor; in-flight frames read the new, empty buffer.                                                                                                      | `Vortex/Upload/UploadHelpers.cpp:143-157`; `Graphics/Common/ResourceRegistry.h:846-960`; `MaterialBinder.cpp:893-925`          | CONFIRMED | S2       |
| C2  | ContactShadowCasterDepthPass binds the camera b1 on an early-submitted list; DepthPrepass resolves draws, WPO and materials through it.                                                                        | `Vortex/Shadows/Passes/ContactShadowCasterDepthPass.cpp:146-211`; `S/Stages/DepthPrepass/DepthPrepass.hlsl:49,70,86,100`       | CONFIRMED | S3       |
| H1  | ViewConstants are one mapped buffer per (slot, view), `memcpy`'d about seven times per view per frame; lists see only the last write.                                                                          | `Vortex/Internal/ViewConstantsManager.cpp:50-126`; `SceneRenderer.cpp` refresh calls                                           | CONFIRMED | S4       |
| H2  | Scene-texture resize unregisters SRVs at once; the LIFO allocator reuses the slot while frames read it.                                                                                                        | `Vortex/SceneRenderer/SceneTextures.cpp:400-413`; `Graphics/Common/Detail/BaseDescriptorAllocator.h:179-205`                   | CONFIRMED | S2       |
| H3  | Geometry eviction and other unregister-then-defer pairs free descriptors immediately.                                                                                                                          | `GeometryUploader.cpp:419-422, 890-914`; `AtlasBuffer.cpp:49-61`; `TextureBinder.cpp:509, 1218, 1404, 2090-2093`               | CONFIRMED | S2       |
| H4  | The shadow pass binds and requires b1 that no shadow shader needs; its shaders still include the ViewConstants cbuffer.                                                                                        | `Vortex/Shadows/Passes/ShadowDepthPass.cpp:485, 660-662`                                                                       | CONFIRMED | S3       |
| H5  | Draw, material, exposure, environment and lighting helpers resolve the view chain implicitly.                                                                                                                  | `LoadResolvedDrawFrameBindings`, `EvaluateMaskedAlphaTest`, `TryLoadMaterialForDraw`                                           | CONFIRMED | S3       |
| H6  | Sky, Fog, GroundGrid and others draw with b1 unbound when the view-constants write failed.                                                                                                                     | `SkyPass.cpp:308-312`; `FogPass.cpp:281-285`; `GroundGridPass.cpp:330-334`                                                     | CONFIRMED | S6       |
| H7  | The shadow VS ignores material WPO; ShadowPassConstants hand-copies six DrawFrameBindings slots.                                                                                                               | `S/Services/Shadows/DirectionalShadowDepth.hlsl`; `ShadowDepthPass.cpp`                                                        | CONFIRMED | S5       |
| H8  | Bindless range checks are inconsistent; materials and draw metadata live outside the global SRV range.                                                                                                         | 53 `BX_IN_*` uses vs 55 files checking only `K_INVALID`                                                                        | CONFIRMED | S6       |
| M1  | Budget rejections are not logged; `Publish` results are written into ViewFrameBindings and b1 unchecked.                                                                                                       | `PerViewStructuredPublisher.h:51-57`; `TransientStructuredBuffer.cpp:200`; `Renderer.cpp:1650-1830`                            | CONFIRMED | S6       |
| M2  | TextureBinder repoints live descriptors (upload complete, eviction, error texture); eviction uses a 2D placeholder for cubes (#25).                                                                            | `TextureBinder.cpp:1394, 2080, 2302`; `ResourceRegistry.h:719`                                                                 | CONFIRMED | S2       |
| M3  | Offscreen sessions trust the caller's sequence and slot; `SceneRenderer::BeginFrame` runs twice per frame.                                                                                                     | `Renderer.cpp:3852-3869, 4607, 4655, 4732`; `SceneRenderer.cpp:1421-1478`                                                      | CONFIRMED | S7       |
| M4  | When the pre-scene publish is skipped, the single-view path builds on another view's bindings.                                                                                                                 | `SceneRenderer.cpp:1966-1972, 3320-3323`                                                                                       | PARTLY    | S7       |
| M5  | Shaders fabricate data: slot 0 as DrawMetadata, identity worlds, depth 1.0 (near under reverse-Z), Earth environment, history = current, exposure 1, masked without material unclipped.                        | `DirectionalShadowDepth.hlsl:48-58`; `BindlessHelpers.hlsl:119-130`; `SceneTextures.hlsli:15`; `MaskedAlphaTest.hlsli:113-114` | CONFIRMED | S6       |
| M6  | `g_PassConstantsIndex` is a ConstantBuffer in some passes and a StructuredBuffer in others; VelocityMerge reuses [0]/[1] as SRVs; literal 0 means "none"; DeferredIbl leaves b2 unset.                         | `Compositing_PS.hlsl:45`; `BasePassVelocityMerge.hlsl:12-15`; `FogPass.cpp:286-291`; `IndirectLightingService.cpp:118-121`     | PARTLY    | S5       |
| M7  | Values arrive by several routes: `exposure_status_uav`, `lighting_frame_slot`, `reverse_z`, view matrices; masked material loaded up to three times per pixel.                                                 | `AtmosphereSkyViewLut.hlsl:42`; `Tonemap.hlsl:34`; `SelectionOutline.hlsl:18`; `GBufferMaterialOutput.hlsli:23,45`             | CONFIRMED | S4, S5   |
| M8  | Hand-mirrored layouts with size-only or no offset asserts; literal offsets 352/360/364; literal sampler slots.                                                                                                 | `DrawFrameBindings.h:81`; `SceneTextures.h:154`; `HdrConsumerInputs.hlsli:41-75`; `SceneDefinitions.hlsli:28`                  | PARTLY    | S8       |
| L1  | Conventional receiver analysis/mask shaders and GPU debug primitive shaders are uncatalogued; their C++ sides are unwired.                                                                                     | `S/Services/Shadows/Conventional/*`; `S/Services/Diagnostics/GpuDebug*`; `RenderingSettingsService.cpp:206`                    | PARTLY    | S10, S11 |
| L2  | `scene_depth_slot` is never published (three scene-depth debug modes output magenta); the procedural-grid material slot is always invalid; skinned/morph slots have no producer or reader (removed until #48). | `ForwardDebug_PS.hlsl:121, 198-199`; `Renderer.cpp:1703`; `InitViewsModule.cpp:276-281`                                        | CONFIRMED | S9       |
| L3  | `post_process_frame_slot` is a CPU control flag that no shader reads.                                                                                                                                          | `SceneRenderer.cpp:1824, 1991, 3013`; `Renderer.cpp:2115`                                                                      | CONFIRMED | S9       |
| L4  | DeferredLightDirectional declares constants it never reads; the CPU still publishes them.                                                                                                                      | `DeferredLightDirectional.hlsl:34-35`; `DeferredLightPass.cpp:791-800`                                                         | CONFIRMED | S9       |
| L5  | `EncodeStaticVelocity` always returns zero and is fed pixel coordinates.                                                                                                                                       | `DepthPrepass.hlsl:33-38, 90`                                                                                                  | CONFIRMED | S9       |
| L6  | Shadow VS mutates and restores `position.z` to compute the biased depth (not a bug; the review's "overwritten" claim was wrong).                                                                               | `DirectionalShadowDepth.hlsl:131-136`                                                                                          | REFUTED   | S9       |
| L8  | `ComputeRenderPass` has no users.                                                                                                                                                                              | `Vortex/Passes/ComputeRenderPass.{h,cpp}`                                                                                      | CONFIRMED | S9       |
| L9  | Dead b1 binds: DistantSkyLightLut (and its gate), SelectionOutline composite.                                                                                                                                  | `DistantSkyLightLutPass.cpp:181, 314-318`; `SelectionOutlinePass.cpp:379-380`                                                  | CONFIRMED | S3       |
| L10 | LocalFogVolume tiled culling keeps retired buffers until destruction; `retired_textures_` is unused.                                                                                                           | `LocalFogVolumeTiledCullingPass.cpp:240-242, 446-451`                                                                          | CONFIRMED | S2       |
| L11 | Shadow occlusion histories are keyed by raw `Texture*` and marked written at record time.                                                                                                                      | `ShadowDepthPass.cpp:90-95, 168-173, 711`; `OcclusionModule.cpp:334`                                                           | PARTLY    | S9       |

## Slices

| ID  | Deliverable                                                            | Findings                    | Depends | State       | Commit |
| --- | ---------------------------------------------------------------------- | --------------------------- | ------- | ----------- | ------ |
| S0  | Design updates and open decisions                                      | all                         | —       | `validated` | —      |
| S1  | Ordered material uploads                                               | N1                          | S0      | `validated` | —      |
| S2  | Receipt-retired descriptors; no live descriptor rewrites               | C1, M2, N2, H2, H3, L10     | S0      | `validated` | —      |
| S3  | Non-camera passes own their inputs; pure shader helpers                | C2, H4, H5, L9              | S0      | `planned`   | —      |
| S4  | Versioned view constants; one source per view value                    | H1, M7                      | S3      | `planned`   | —      |
| S5  | One pass-input convention; shared mesh position with WPO               | H7, M6, M7                  | S3, S4  | `planned`   | —      |
| S6  | Fail loudly                                                            | M1, M5, H6, H8              | S4      | `planned`   | —      |
| S7  | Frame and view lifecycle                                               | M3, M4                      | S4      | `planned`   | —      |
| S8  | Generated shared contracts                                             | M8, H8                      | S5, S6  | `planned`   | —      |
| S9  | Unwired fields and dead code                                           | L2, L3, L4, L5, L6, L8, L11 | S3      | `planned`   | —      |
| S10 | Conventional shadow receiver analysis and receiver-mask caster culling | L1                          | S3, S5  | `planned`   | —      |
| S11 | GPU debug primitives                                                   | L1, VX-DIAG-01              | S3, S6  | `planned`   | —      |
| S12 | Closeout                                                               | all                         | S1–S11  | `planned`   | —      |

S1 and S2 come first: they remove the remaining causes of garbage material
reads. S9 can run in parallel with S4–S8.

## S0 — Designs

Update the owning designs, record the [answered decisions](#decisions).

- [x] **Graphics.** [Descriptor lifetime](../../../../src/Oxygen/Graphics/Common/README.md#descriptor-lifetime):
      published descriptors are immutable, `Graphics::RetireResource`, debug
      FIFO slot reuse.
- [x] **Upload.** [upload.md](../../lld/upload.md): binder uploads on the
      graphics-queue inline path (GD-1); growth by GPU copy into a new buffer
      and descriptor (GD-2).
- [x] **Shader contracts.** [shader-contracts.md](../../lld/shader-contracts.md):
      per-pass inputs record, root-constant convention, pure helpers vs
      bound-view header, fail-loud rules, versioned b1 (GD-3).
- [x] **Shadows.** [shadow-service.md](../../lld/shadow-service.md): shadow and
      contact-shadow inputs records, shared mesh position with WPO, receiver
      analysis and receiver-mask culling, GPU-driven cascades (GD-6), with
      UE5.7 grounding.
- [x] **Diagnostics.** [diagnostics-service.md](../../lld/diagnostics-service.md):
      GPU debug primitive buffers, pass scheduling, budget and feature gate.
- [x] **Motion.** [runtime-motion-producers.md](../../lld/runtime-motion-producers.md):
      skinned/morph producers wait for the animation module (#48); their
      publication slots leave DrawFrameBindings until then.

## S1 — Ordered material uploads

- [x] `Vortex/Renderer.{h,cpp}`: a second `UploadCoordinator` on the graphics
      queue (`GetGraphicsUploadCoordinator`, `GetGraphicsStagingProvider`).
- [x] `InitViewsModule.cpp`: MaterialBinder uploads through it, so material
      and procedural-grid copies are submitted during view preparation, before
      the view's passes, in graphics-queue order (GD-1).
- [x] `Vortex/Resources/MaterialBinder.cpp`: a changed element stays pending
      until a submission takes it; a failed submission retries next frame
      instead of being dropped at frame start; each view's preparation uploads
      only what is still pending.
- [x] **Tests.** `MaterialBinderEdgeTest.LaterViewMaterialIsUploadedInSameFrame`;
      the 35 MaterialBinder tests and `SceneRendererDeferredCore` pass.

## S2 — Receipt-retired descriptors

- [x] **Retirement.** `Graphics::RetireResource` (strong exception
      guarantee) replaces the duplicated unregister-then-defer helpers
      (StructuredGpuBuffer, environment, light grid, HZB, base pass, selection
      outline, shadow targets, scene textures, atlas, geometry).
- [x] **Growth.** `CreateBufferAndSrv` replaces `EnsureBufferAndSrv`; the
      in-place `Replace` path is gone. `AtlasBuffer` growth GPU-copies on the
      graphics queue and publishes a new SRV (GD-2); element refs survive
      growth. Growth now happens only when a request does not fit (it used to
      resize, and rewrite the live descriptor, on most allocations).
- [x] **Geometry.** Each content version gets a new buffer and SRV; the old
      one retires by receipt.
- [x] **Local fog.** Grown buffers retire at once (L10).
- [x] **Debug aid.** Debug builds use fresh bindless slots before recycling,
      oldest first.
- [x] **Accessors.** `Graphics::GetDescriptorAllocator()`/`GetResourceRegistry()`
      are real overloads in every backend, mock and fake (no `const_cast`).
- [x] **TextureBinder (S2c).** A loaded texture version registers its own
      SRV; nothing is repointed (`UpdateView` and the per-entry placeholders
      are gone). Loading entries publish the shared placeholder, failures the
      shared error binding; both exist per shape (2D, cube, 2D array), so an
      evicted or rejected cube publishes a cube view (#25). Replaced and
      evicted versions retire through `Graphics::RetireResource`. Entry
      identity is a Nexus `IndexReuse` handle, finalized at once because no
      GPU work reads it; loads, completions, retries and evictions validate
      it. Descriptor revisions come from one counter. `IResourceBinder`
      exposes `GetResidentContentRevision()`; MaterialBinder re-resolves a
      material when it changes and uploads it only if an index changed. A
      failed view registration keeps the completion queued for retry.
- [x] **Fallback ordering (N2).** The shared placeholder and error textures
      are managed registrations initialized through
      `PrepareImmutableTexture2D` (now also for cubes) and recorded on the
      graphics queue at construction, so no pass samples them before their
      copy lands (GD-1).
- [x] **Tests.** Retired buffer keeps views until slot reuse; atlas growth
      copies, new SRV, no growth within capacity; geometry eviction and update
      follow retirement; texture versions get new descriptors and never
      rewrite the placeholder; cube eviction and cube rejection publish cube
      views; failed publication retries; materials re-serialize on index
      change.

## S3 — Non-camera passes own their inputs

- [ ] **Helpers.** Split `S/Contracts/Draw/DrawHelpers.hlsli`,
      `S/Shared/MaskedAlphaTest.hlsli` and the exposure, environment and
      lighting helpers into pure (slot/record) helpers and one bound-view
      header guarded by `#error` outside camera entry points.
- [ ] **Shadow pass.** `ShadowDepthPass.cpp`: drop the b1 bind and gate;
      `DirectionalShadowDepth.hlsl` includes pure helpers only.
- [ ] **Contact shadows.** `ContactShadowCasterDepthPass.cpp`: its own inputs
      record and a depth shader that reads only that record.
- [ ] **Dead binds.** `DistantSkyLightLutPass.cpp` (bind, gate and the unused
      `device_z` parameter); `SelectionOutlinePass.cpp` composite bind.
- [ ] **Tests.** ShaderBake fails a non-camera entry point that includes the
      bound-view header; GPU test: contact-shadow and shadow depth are
      unchanged when b1 is rewritten after their submission.

## S4 — Versioned view constants

- [ ] `Vortex/Internal/ViewConstantsManager.cpp`, `Renderer.cpp`: each write
      allocates a new transient record bound by GPU address (GD-3); passes see the values
      current at record time.
- [ ] One source each for view matrices, `reverse_z`, `exposure_status_uav`
      and `lighting_frame_slot`; remove the duplicates from pass constants
      (Tonemap, Compositing, SelectionOutline, atmosphere LUT passes).
- [ ] **Tests.** A list submitted mid-view reads the bindings published before
      it, not later ones; the staged-publication CHECK is backed by a GPU
      assertion.

## S5 — Pass-input convention and mesh position

- [ ] **Convention.** `g_PassConstantsIndex` is a StructuredBuffer record in
      every pass; `K_INVALID_BINDLESS_INDEX` when a pass has none; a named
      root-constant layout for `BasePassVelocityMerge.hlsl`; DeferredIbl binds
      b2.
- [ ] **Mesh position.** One `ComputeMeshWorldPosition` (with material WPO)
      shared by depth prepass, base pass, shadow and contact-shadow passes;
      shadow constants carry `draw_frame_slot` instead of copied slots.
- [ ] **Material once.** The masked base pass loads the material once per
      pixel and passes it down.
- [ ] **Tests.** A WPO-animated caster's shadow matches its camera geometry;
      ShaderBake catalog passes; existing image-producing GPU suites unchanged.

## S6 — Fail loudly

- [ ] `PerViewStructuredPublisher`, `TransientStructuredBuffer`: `Publish`
      returns `expected`; budget rejections log once per frame per label.
- [ ] `Renderer.cpp`: a missing required slot rejects the view with a logged
      reason.
- [ ] Passes reject a missing required input instead of drawing with an
      unbound root parameter (Sky, Fog, GroundGrid, AtmosphereCompose,
      LocalFogVolume passes).
- [ ] Shaders: invalid inputs early-out; remove fabricated fallbacks (M5);
      typed domain checks per contract (H8); Debug builds record invalid
      accesses in the diagnostics buffer.
- [ ] **Tests.** Budget exhaustion rejects the view and logs; a pass without
      view constants records nothing; an invalid material slot is reported,
      not drawn unclipped.

## S7 — Frame and view lifecycle

- [ ] `Renderer.cpp`: offscreen and standalone sessions validate sequence and
      slot against the renderer's frame.
- [ ] `SceneRenderer.cpp`: split `BeginFrame` into once-per-frame and per-view
      parts; no second call inside a frame; `GetPublishedViewFrameBindings`
      checks the view id; the single-view path resets per-view products.
- [ ] **Tests.** An offscreen render inside a host frame leaves the host
      view's state intact; a mismatched session is rejected; the single-view
      path without post-process never inherits another view's draw slot.

## S8 — Generated shared contracts

- [ ] Extend BindlessCodeGen (`Core/Meta/Bindless.yaml`) to emit shared
      structs (C++ and HLSL with offset asserts), sampler slots and typed
      accessors with their domain checks.
- [ ] Move DrawFrameBindings, ViewConstants, ViewFrameBindings,
      ViewHistoryFrameBindings, DrawMetadata, SceneTextureBindings, occlusion
      records, shadow and atmosphere/fog pass constants and the
      `HdrConsumerInputs` offsets to the schema.
- [ ] **Tests.** Generator tests; layout mismatch fails the build.

## S9 — Unwired fields and dead code

- [ ] Publish `scene_depth_slot`; the scene-depth debug modes render depth.
- [ ] Wire `procedural_grid_material_constants_slot` and its forward
      evaluation path with a test, per the ground-grid design.
- [ ] Replace `post_process_frame_slot` as a CPU flag with explicit state;
      drop the field unless a shader consumes it.
- [ ] Remove DeferredLightDirectional's unused constants and their CPU record.
- [ ] Replace `EncodeStaticVelocity` with a direct zero.
- [ ] Shadow VS: compute the biased depth from a local.
- [ ] Delete `ComputeRenderPass` and its CMake entries.
- [ ] Remove the skinned/morph publication slots from DrawFrameBindings (C++,
      HLSL, `InitViewsModule.cpp`); #48 reintroduces them with their producer.
- [ ] Shadow occlusion history keyed by texture generation and marked written
      on submission (main view too).
- [ ] **Tests.** Debug-mode image tests for scene depth; procedural-grid
      material test; occlusion history reset on surface recreation.

## S10 — Conventional receiver analysis and caster culling

Design: S0 shadows update (GD-6: GPU-driven, no readback).

- [ ] Catalog `ConventionalShadowReceiverAnalysis.hlsl` and
      `ConventionalShadowReceiverMask.hlsl`; port them to the S3/S5 input
      model.
- [ ] C++ passes and `ConventionalShadowDrawRecord` wiring in ShadowService:
      tight per-cascade bounds from scene depth, tile receiver masks, caster
      culling ahead of two-phase occlusion; shadow inputs records and shadow
      occlusion read the cascade matrices from the GPU buffer.
- [ ] **Tests.** CPU reference for bounds and masks; GPU test that culled
      casters cast no visible shadow; image-producing shadow suites pass.

## S11 — GPU debug primitives

Design: S0 diagnostics update.

- [ ] Catalog `GpuDebugClear.hlsl` and `GpuDebugDraw.hlsl`; port them to the
      input model.
- [ ] `GpuDebugPrimitivePass`, bounded buffers, `DebugFrameBindings`
      publication, `DiagnosticsFeature::kGpuDebugPrimitives` gate,
      `FramePlanBuilder` wiring and the DemoShell toggle
      (`RenderingSettingsService::SetGpuDebugPassEnabled` applies it).
- [ ] **Tests.** Primitives emitted by a test shader appear in the image;
      overflow is bounded and reported; disabled path allocates nothing.

## S12 — Closeout

- [ ] All findings closed or linked to an issue with an `OPEN_ITEMS` row.
- [ ] Designs current; `CheckDocumentation.py` passes; `STATUS.md`
      regenerated.
- [ ] **Owner.** RenderScene with BurgerPiz at `-r 30 --vsync on`, with GPU
      crash dumps enabled: no flicker and no device removal.
- [ ] **Owner.** Editor check of the ED-M08 delivered slices; ED-M08 resumes.
