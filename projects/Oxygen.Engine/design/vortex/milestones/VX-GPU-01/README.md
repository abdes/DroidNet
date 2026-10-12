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
| S3  | Non-camera passes own their inputs; pure shader helpers                | C2, H4, H5, L9              | S0      | `validated` | —      |
| S4  | Versioned view constants; one source per view value                    | H1, M7                      | S3      | `validated` | —      |
| S5  | One pass-input convention; shared mesh position with WPO               | H7, M6, M7                  | S3, S4  | `validated` | —      |
| S6  | Fail loudly                                                            | M1, M5, H6, H8              | S4      | `validated` | —      |
| S7  | Frame and view lifecycle                                               | M3, M4                      | S4      | `validated` | —      |
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

- [x] **Helpers.** `ViewConstants.hlsli` is the bound-view header, guarded
      by `#error` unless ShaderBake defines `OXYGEN_CAMERA_VIEW_ENTRY`, which
      it does only for catalog entries marked `.view_binding=kCamera`. The
      define is in the action key, not the request identity. Camera-only
      helpers reach b1 through it; pure counterparts take slots or records:
      `MaskedAlphaTest.hlsli` (bound part in `MaskedAlphaTestView.hlsli`),
      `FrameExposure.hlsli`, `SkyDiffuseIrradiance.hlsli`,
      `LightRecords.hlsli`, and `ComputeMeshClipPosition` with explicit
      matrices. `SkyRadiance.hlsli` and `AtmosphereSampling.hlsli` no longer
      include bound helpers.
- [x] **Shadow pass.** `ShadowDepthPass` no longer binds or requires b1;
      `DirectionalShadowDepth.hlsl` reaches only pure helpers.
- [x] **Contact shadows.** `ContactShadowCasterDepth.hlsl` reads one inputs
      record (view and projection matrices at record time, the view's
      published `draw_frame_slot`); `PreparedViewShadowInput` carries that
      slot instead of the view-constants buffer.
- [x] **Dead binds.** `DistantSkyLightLutPass` bind and gate removed; the
      unused `device_z` atmosphere parameter and its `reverse_z` read are gone,
      so the multi-scattering, distant-sky and capture LUTs no longer read an
      unbound b1. The sky-view LUT's camera entry passes exposure to the shared
      generator, and its capture entry compiles without the bound view. The
      `SelectionOutlinePass` composite bind is removed.
- [x] **Tests.** ShaderBake compiles the bound-view header as a camera entry
      and rejects it otherwise; the camera define changes the action key.
      Shadow depth and contact-caster depth draw without binding b1, and the
      caster depth records nothing without the view's draw publication. The
      bake now rejects any non-camera entry that can read b1, so a b1 rewrite
      after submission cannot reach these passes. That replaces the planned
      GPU rewrite test, and the GPU contact-shadow image tests still pass.

## S4 — Versioned view constants

- [x] **Versioned b1.** Each view-constants write is a new record placed by
      `upload::TransientConstantBuffer` (new, reusable for any root-CBV
      record) in the renderer's shared staging ring; `RenderContext` holds a
      `ConstantBufferRecord` (buffer and offset) bound by GPU address. Passes
      see the values current at record time. `ViewConstantsManager`, its
      per-view buffers and its removal bookkeeping are gone.
      `StagingProvider::AllocateAligned` is the one aligned sub-allocation
      both transient types use (stride or 256-byte placement).
- [x] **One source per view value.** Camera entries read `reverse_z`
      (SelectionOutline) and `exposure_status_uav` (aerial perspective,
      volumetric fog, sky-view LUT, sky, local fog) from the bound view
      through `GetExposureStatusUav()`; the capture sky-view entry has none.
      View history no longer copies the current view, projection and inverse
      view-projection matrices. Tonemap and exposure records carry the view's
      `ViewFrameBindings` slot, set beside each b1 record, and read the
      lighting slot from it; the `current_view.lighting_frame_slot` mirror is
      removed. Compositing runs outside view recording and keeps resolving the
      lighting slot from `LightingService`, its owner, so its record's slot
      has that one source.
- [x] **Reuse.** The 14 per-pass copies of the root-binding builder use
      `internal::BuildVortexRootBindings`; atmosphere passes share
      `AtmosphereGpuPacking.h` (vector, light and ozone-layer rows) and no
      longer default their records to Earth values. DXC test probes build
      through one `oxygen_add_dxc_compute_probe` CMake function.
- [x] **Tests.** `ExposureGpuTest.PassRecordedMidViewReadsTheBindingsPublishedBeforeIt`:
      two probe dispatches in one command list, around a republish that
      mirrors Stage 9 → Stage 10, read the Stage 9 bindings (no SceneColor)
      and then the Stage 10 ones on the GPU, backing the staged-publication
      CHECK. `TransientConstantBuffer` unit tests (aligned new record per
      write, records outlive ring growth, empty write rejected); runtime view
      publication writes a new record per materialization.

## S5 — Pass-input convention and mesh position

- [x] **Convention.** `g_PassConstantsIndex` is a `StructuredBuffer` record in
      every pass, written through the Upload transient buffers;
      `K_INVALID_BINDLESS_INDEX` when a pass has none (Sky, Fog,
      AtmosphereCompose, ViewPick, DeferredIbl, which now binds b2).
      Compositing and deferred lights lose their hand-rolled CBV publishers:
      compositing writes a `TransientStructuredBuffer` record; deferred local
      lights publish one record array per Record, each draw selecting its
      element at `[0]`, and directional lights read no inputs (L4). The
      velocity merge reads its SRVs from an inputs record instead of raw root
      constants.
- [x] **Mesh position.** `Contracts/Draw/MeshPosition.hlsli`
      (`ResolveDrawWorldOffset`, `ComputeMeshWorldPosition`,
      `ComputeMeshClipPosition`) is shared by depth prepass, base pass,
      velocity, shadow and contact-shadow passes. Shadow constants carry the
      view's `draw_frame_slot` instead of five copied slots; shadow depth
      records nothing without it, and its biased depth comes from a local
      (L6).
- [x] **Material once.** The masked base pass loads the draw material once
      (`TryLoadDrawMaterialFromSlots`) and feeds the alpha clip, surface
      evaluation, shading model and GBuffer packing from it.
- [x] **Tests.** `GrazingDirectionalShadowTest.WorldPositionOffsetMovesShadow`:
      a material WPO offset moves the shadow exactly as the same transform
      offset does. `ShadowDepthNeedsTheViewDrawPublication`. The shader bake,
      ShadowService (53), the deferred-core, lighting-image, shadow-admission
      and exposure GPU cases covering composition, tonemap, fog, sky and
      deferred lights pass.

## S6 — Fail loudly

- [x] `PerViewStructuredPublisher`, `TransientStructuredBuffer`: `Publish`
      returns `expected`; budget rejections log once per frame per label.
- [x] `Renderer.cpp`: a missing required slot rejects the view with a logged
      reason.
- [x] Passes reject a missing required input instead of drawing with an
      unbound root parameter (Sky, Fog, GroundGrid, AtmosphereCompose,
      LocalFogVolume passes).
- [x] Shaders: invalid inputs early-out; remove fabricated fallbacks (M5);
      typed domain checks per contract (H8).
- [x] **Tests.** A publication failure rejects the view and the next view
      recovers; a pass without view constants records nothing.

Notes:

- Required records deep inside pass helpers (exposure, wireframe constants)
  throw through `RequirePublished`; the scene renderer's per-view handler
  rejects the view with the reason instead of the former `CHECK_F` abort.
- Environment publication failures now reject the view. The camera
  aerial-perspective pass read its holdout flag from environment data that is
  published after it records; the flag now travels in its pass constants.
- The forward mesh vertex shader used its own copies of the draw and
  transform loads and ignored material WPO; it now uses the shared helpers and
  `ComputeMeshWorldPosition`.
- Draw metadata, bounds and cull records live in the materials domain; the
  draw-metadata load checks that domain.
- Fixtures that render camera passes now publish a unit frame exposure; the
  shaders no longer supply one.
- Recording invalid accesses in the diagnostics buffer, and reporting an
  invalid material slot through it, move to S11, which builds that buffer.

## S7 — Frame and view lifecycle

- [x] `Renderer.cpp`: offscreen and standalone sessions validate sequence and
      slot against the renderer's frame (`CheckFrameSession`).
- [x] `SceneRenderer.cpp`: split `BeginFrame` into once-per-frame and per-view
      parts; no second call inside a frame; `GetPublishedViewFrameBindings`
      checks the view id; the single-view path resets per-view products.
- [x] **Tests.** An offscreen render inside a host frame leaves the host
      view's state intact; a mismatched session is rejected; the single-view
      path without post-process never inherits another view's records.

Notes:

- `BeginFrame` runs once per (sequence, slot). `BeginViewFamily` resets what a
  render call selects and publishes (lights, light grid, lighting state,
  per-view products) at the start of each family; a scoped single view resets
  only its own products.
- In-frame offscreen renders no longer re-run the scene renderer's frame start
  after rendering; the host family starts clean on its own.
- The single-view path always publishes its pre-scene record, and the
  post-scene refresh rejects a view whose pre-scene record is another view's.

## S8 — Generated shared contracts

- [ ] Extend BindlessCodeGen (`Core/Meta/Bindless.yaml`) to emit shared
      structs (C++ and HLSL with offset asserts), sampler slots and typed
      accessors with their domain checks.
- [ ] Move DrawFrameBindings, ViewConstants, ViewFrameBindings,
      ViewHistoryFrameBindings, DrawMetadata, SceneTextureBindings, occlusion
      records, shadow, contact-shadow caster and atmosphere/fog pass
      constants and the `HdrConsumerInputs` offsets to the schema.
- [ ] **Tests.** Generator tests; layout mismatch fails the build.

## S9 — Unwired fields and dead code

- [ ] Publish `scene_depth_slot`; the scene-depth debug modes render depth.
- [ ] Wire `procedural_grid_material_constants_slot` and its forward
      evaluation path with a test, per the ground-grid design.
- [ ] Replace `post_process_frame_slot` as a CPU flag with explicit state;
      drop the field unless a shader consumes it.
- [x] Remove DeferredLightDirectional's unused constants and their CPU record.
- [ ] Replace `EncodeStaticVelocity` with a direct zero.
- [x] Shadow VS: compute the biased depth from a local.
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
- [ ] Debug builds record invalid bindless accesses in the debug buffer
      (from S6); an invalid material slot is reported there.
- [ ] **Tests.** Primitives emitted by a test shader appear in the image;
      overflow is bounded and reported; disabled path allocates nothing; an
      invalid material slot is reported, not drawn.

## S12 — Closeout

- [ ] All findings closed or linked to an issue with an `OPEN_ITEMS` row.
- [ ] Designs current; `CheckDocumentation.py` passes; `STATUS.md`
      regenerated.
- [ ] **Owner.** RenderScene with BurgerPiz at `-r 30 --vsync on`, with GPU
      crash dumps enabled: no flicker and no device removal.
- [ ] **Owner.** Editor check of the ED-M08 delivered slices; ED-M08 resumes.
