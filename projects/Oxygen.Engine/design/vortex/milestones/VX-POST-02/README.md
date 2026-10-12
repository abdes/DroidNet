# VX-POST-02 — Temporal resolve (TSR)

Status: `planned`

| Field     | Summary                                                                                                    |
| --------- | ---------------------------------------------------------------------------------------------------------- |
| Outcome   | None yet. Vortex has no anti-aliasing and no temporal resolve; jitter plumbing exists but nothing sets it. |
| Remaining | Decisions recorded; S1–S9 remain. Starts after VX-VIEW-03.                                                 |
| Evidence  | Gates in [temporal-resolve.md §12](../../lld/temporal-resolve.md#12-validation-gates).                     |
| Issue     | [abdes/DroidNet#59](https://github.com/abdes/DroidNet/issues/59)                                           |

This plan resolves [#15](https://github.com/abdes/DroidNet/issues/15) (temporal color resolve with exposure-correct
history) and the missing-TAA root cause of [#20](https://github.com/abdes/DroidNet/issues/20) (volumetric fog
shimmer).

The design owns the algorithm, every format and every gate:
[temporal-resolve.md](../../lld/temporal-resolve.md), built on
[camera-relative-rendering.md](../../lld/camera-relative-rendering.md). This
plan owns only order, files, tests, owner actions and status. Paths are
relative to `projects/Oxygen.Engine/src/Oxygen/`; shaders are under
`Graphics/Direct3D12/Shaders/Vortex/`.

## Resume

After a context compaction or in a new session:

1. Find the first slice in the [slice table](#slices) that is not
   `validated`.
2. Open its section and continue at the first unchecked box.
3. Run `git status` and `git log --oneline -8`.
4. Re-read only the LLD sections the slice cites.

Update the slice table, the checkboxes and the header table as work lands.
Commit messages describe the change; they never cite this plan or its IDs.

## Decisions

Decided 2026-10-12; rationale in [LLD §2](../../lld/temporal-resolve.md#2-decisions).

**Prerequisite.** Camera-relative rendering
([VX-VIEW-03](../VX-VIEW-03/README.md)) is delivered before S1: velocity,
reprojection and history matrices are built on its translated, double-built
view data.

| Decision | Topic                      | Value                                                                        | Status  |
| -------- | -------------------------- | ---------------------------------------------------------------------------- | ------- |
| TR-D1    | Algorithm                  | TSR-class resolve after UE5.7; no classic TAA phase                          | decided |
| TR-D2    | Feature scope              | Full TSR pass set; resurrection, reprojection fields, async compute deferred | decided |
| TR-D3    | Upscaling                  | Per-view screen percentage 50–100 %                                          | decided |
| TR-D4    | Velocity contract          | `RG16_FLOAT` + `R8_UINT` flags; camera motion from depth                     | decided |
| TR-D5    | Jitter                     | Halton (2,3), prime sequence length, renderer-owned                          | decided |
| TR-D6    | Temporal sampling contract | One published frame index, sequences and blue noise                          | decided |
| TR-D7    | Placement                  | First pass of Stage 22 on Stage 21's resolved color; metering unchanged      | decided |
| TR-D8    | History domain             | `RGBA16_FLOAT` scaled by `E`, rebased; FP32 scene-referred output            | decided |
| TR-D9    | History ownership          | Per `ViewStateHandle`; one owner for every per-view history                  | decided |
| TR-D10   | Material flags             | Cooked properties feeding the existing motion capability path                | decided |
| TR-D11   | Stateless views            | Spatial AA only; captures use a state handle and warm-up                     | decided |
| TR-D12   | Post-resolve depth         | Output-resolution closest depth for editor aids                              | decided |

## Findings

Current code against the design. C++ paths are under `src/Oxygen/`; shader
paths are under the Vortex shader root.

| ID  | Finding                                                                                                                                                                                                                         | Evidence                                                                                                                                                                                | Slice |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----- |
| T1  | Stage 3 writes zero velocity and publishes `kSceneVelocity`, contradicting the depth-prepass design.                                                                                                                            | `Stages/DepthPrepass/DepthPrepass.hlsl:72-92`; `Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp:55-107`; `Vortex/SceneRenderer/SceneRenderer.cpp:2017, 2280, 3239-3247` | S1    |
| T2  | Forward-shaded opaque views write no velocity, and forward is the default shading mode.                                                                                                                                         | `Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:1055`; `Vortex/SceneRenderer/SceneRenderer.h:337`                                                                              | S1    |
| T3  | Velocity uses the jittered projections and has no written flag, so camera motion cannot be told from object motion. `ViewHistoryFrameBindings` already publishes stable projections and both jitters, but no shader reads them. | `Stages/BasePass/BasePassGBuffer.hlsl:92-179`; `Stages/BasePass/BasePassVelocityAux.hlsl:104-121`; `Contracts/View/Generated.ViewContracts.hlsli:73-88`                                 | S1    |
| T4  | An invalid view history sets previous = current per vertex, zeroing object motion as well as camera motion.                                                                                                                     | `Stages/BasePass/BasePassGBuffer.hlsl:145-147`; `Stages/BasePass/BasePassVelocityAux.hlsl:118-121`                                                                                      | S1    |
| T5  | No jitter source: the resolver builds a fresh view whose `pixel_jitter` nothing sets; `CompositionView` jitter is copied into the view context but the resolver drops it.                                                       | `Vortex/SceneCameraViewResolver.cpp:162, 183`; `Core/Types/View.h:37`; `Vortex/Renderer.cpp:2475`                                                                                       | S2    |
| T6  | `PreviousViewHistoryCache::EndFrame` rolls every touched entry forward unconditionally, although the architecture rolls history only after a successful frame. The rigid-transform and deformation caches do the same.          | `Vortex/Internal/PreviousViewHistoryCache.cpp:100-112`; `Vortex/Renderer.cpp:2391-2393, 3992`; ARCHITECTURE §6.3.1                                                                      | S2    |
| T7  | Volumetric fog and screen HZB histories are keyed by `ViewId`, not `ViewStateHandle`.                                                                                                                                           | `Vortex/Environment/Passes/VolumetricFogPass.h:134`; `Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.cpp:374`                                                                          | S2    |
| T8  | Volumetric fog keeps a private Halton sequence indexed by the global frame sequence; the generated blue-noise data is unused and has no GPU texture.                                                                            | `Vortex/Environment/Passes/VolumetricFogPass.cpp:76-107, 406-420`; `Vortex/Internal/BlueNoiseData.h`                                                                                    | S7    |
| T9  | The `post_history_srv` handoff is published but unused, waiting on exposure-correct conversion (issue 15). No shader reads `PostProcessFrameBindings` at all.                                                                   | `Vortex/PostProcess/PostProcessService.cpp:494-499, 533-546`                                                                                                                            | S3    |
| T10 | `CompositingTaskType::kTaa` is a placeholder that logs "not implemented"; temporal AA is not a compositing task.                                                                                                                | `Vortex/Types/CompositingTask.h:32, 61-64`; `Vortex/Renderer.cpp:536-538, 2328-2331`                                                                                                    | S3    |
| T11 | Scene textures, leases and the post chain assume render extent = output extent (`render_scale_milli` fixed at 1000).                                                                                                            | `Vortex/SceneRenderer/SceneTextureLeasePool.h:28-45`; `Vortex/SceneRenderer/SceneRenderer.cpp:3602-3616`                                                                                | S6    |

## Working rules

- **Commits.** One thematic commit per slice once its tests pass; no review
  pause. Work stops only at **Owner** boxes and for decisions I am not
  confident about, presented with facts and pros and cons.
- **GPU tests.** One GPU test process at a time, filtered to the tests being
  worked on. A test that removes the D3D12 device is never rerun by me; the
  owner gets the command.
- **Builds.** I build engine targets and tests in Debug. The owner builds the
  SDK and the editor, runs the editor and re-cooks content.
- **Contracts.** Layout changes go through the generated schema; no
  hand-written mirrors. The hand-written velocity records
  (`VelocityDrawMetadata`, `MaterialWpoPublication`,
  `MotionVectorStatusPublication`, `VelocityMergeInputs`) move into the
  schema in S1.
- **Issues.** Work deferred out of a slice becomes an abdes/DroidNet issue
  and an `OPEN_ITEMS` row in the same change. An issue this plan resolves is
  closed with its evidence when the resolving slice lands.
- **Docs.** Update owner documents in the slice that changes the behavior
  they describe: base-pass, depth-prepass, scene-textures, post-process,
  exposure, multi-view-composition, translucency, editor-rendering,
  shader-contracts, ARCHITECTURE.

Commands, from `projects/Oxygen.Engine` after `vcvars64`:

```text
cmake --build --preset oxygen-ninja-debug --target <targets>
ctest --preset oxygen-ninja-debug -R '^<test>$' --output-on-failure
./tools/cli/oxyrun.ps1 <test> -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=<case>
./tools/cli/oxyformat.ps1 <files> --fix
./tools/cli/oxytidy.ps1 <files> --configuration Debug --include-tests --build-dir out/build-ninja
python tools/vortex/CheckDocumentation.py
```

## Slices

| Slice | Scope                                            | Depends on     | Status      |
| ----- | ------------------------------------------------ | -------------- | ----------- |
| S0    | Decisions                                        | —              | `validated` |
| S1    | Velocity contract                                | S0, VX-VIEW-03 | `planned`   |
| S2    | Jitter, temporal sampling and history ownership  | S1             | `planned`   |
| S3    | TSR core at native resolution                    | S2             | `planned`   |
| S4    | Rejection quality: flicker, moiré, thin geometry | S3             | `planned`   |
| S5    | Translucent velocity and material flags          | S3             | `planned`   |
| S6    | Screen percentage and history resolution         | S4             | `planned`   |
| S7    | Stochastic consumers and diagnostics             | S3             | `planned`   |
| S8    | Editor aids and stateless views                  | S6             | `planned`   |
| S9    | Closeout                                         | S1–S8          | `planned`   |

### S0 — Decisions

- [x] Decide TR-D1–TR-D12 (2026-10-12).
- [x] Record them in the LLD, this plan, OPEN_ITEMS, PLAN and
      VX-SHADOW-01.

### S1 — Velocity contract

Design: LLD §5. Findings T1–T4.

- [ ] `SceneTextures`: `SceneTemporalFlags` (`R8_UINT`) beside
      `SceneVelocity`; scene-texture bindings publish both; Stage 9 publishes
      `kSceneVelocity` for every lit view.
- [ ] Velocity records into the schema (working rules).
- [ ] Depth prepass: remove the `HAS_VELOCITY` pixel variant, the velocity
      attachment, `DepthPrepassConfig::write_velocity` and the `kSceneVelocity`
      publication.
- [ ] Base pass (deferred and forward), `BasePassVelocityAux.hlsl`,
      `BasePassVelocityMerge.hlsl`: stable matrices; velocity and flags for
      changed draws, cleared flag for unchanged ones; the merge sets the
      flag.
- [ ] `TransformUploader`: add a world-transform-unchanged draw flag to the
      draw contract schema and set it.
- [ ] Invalid view history: previous = current only for the camera; object
      motion stays.
- [ ] **Tests.** G1: static scene and camera give zero velocity; moving object
      velocity matches its transform; forward views write velocity; Stage 3
      no longer publishes velocity.

### S2 — Jitter, temporal sampling and history ownership

Design: LLD §4, §7. Findings T5–T7.

- [ ] `Renderer.cpp`, `SceneCameraViewResolver.cpp`: jitter sequence per view
      state (prime length, discontinuity reset); remove the external
      `pixel_jitter` inputs on `View` and `CompositionView`.
- [ ] `ViewConstants` temporal fields (§4).
- [ ] Temporal-history owner keyed by `ViewStateHandle`; every per-view
      history moves to it (LLD §7): volumetric fog, screen HZB, occlusion
      (`OcclusionModule.cpp:121`), `RetainedTexturePool` artifacts, ground
      grid (`GroundGridPass.h:91`), cascade quality
      (`CascadeShadowPass.h:131`) and shadow occlusion. Roll-forward only
      after a successful view frame, also for `PreviousViewHistoryCache`,
      `RigidTransformHistoryCache` and `DeformationHistoryCache`
      (`Renderer.cpp:2391-2393, 3992`).
- [ ] Material mip bias from `texture_mip_bias`.
- [ ] Volumetric fog reprojects with the stable matrices.
- [ ] **Tests.** Sequence length and values for 100 % and 50 %; history
      survives a failed view frame unchanged; every moved history resets on
      handle change, not on `ViewId` reuse.

### S3 — TSR core at native resolution

Design: LLD §3, §6, §7. Findings T9, T10.

- [ ] `PostProcess/TemporalResolve/`: input record, history set, and the
      ClearPrevAtomics, DilateVelocity, DecimateHistory, RejectShading
      (guide only), UpdateHistory and ResolveHistory passes.
- [ ] Shaders under `Services/PostProcess/TemporalResolve/`; catalog entries.
- [ ] Exposure-scaled history with rebase; remove `post_history_srv` and the
      `kTaa` compositing placeholder.
- [ ] Stage 22 runs the resolve first on Stage 21's resolved scene color
      (pre-exposed half or FP32 with its exposure); bloom and tonemap read
      its output.
- [ ] **Tests.** G2, G3, G4, G5, G7.

### S4 — Rejection quality

Design: LLD §6.2.

- [ ] Moiré history and frame-rate-scaled flicker detection; thin-geometry
      detection and coverage history; SpatialAntiAliasing.
- [ ] **Tests.** G9.

### S5 — Translucent velocity and material flags

Design: LLD §5, §8. Decision TR-D10.

- [ ] Material asset properties `temporal_responsiveness`,
      `has_pixel_animation`, `output_translucent_velocity`: cooked format,
      cooker and loader, feeding `MaterialMotionCapabilityFlags`
      (`SceneSync/RuntimeMotionProducerModule.h`) and
      `MotionPublicationCapabilityBits` (`Vortex/Types/VelocityPublications.h`),
      which gains the translucent-velocity bit. Shaders read the bits.
- [ ] Stage 19 translucent velocity pass (depth, velocity and flags with the
      material clip).
- [ ] **Tests.** G8; cooker round-trip tests for the new properties.
- [ ] **Owner.** Re-cook content with the new material format.

### S6 — Screen percentage and history resolution

Design: LLD §4, §6.2. Finding T11.

- [ ] Per-view screen percentage and history percentage settings; scene
      textures and leases at render extent; post chain at output extent.
- [ ] ResolveHistory downsampling for history above 100 %.
- [ ] **Tests.** G6.

### S7 — Stochastic consumers and diagnostics

Design: LLD §4, §10. Finding T8.

- [ ] Blue-noise texture array created from `BlueNoiseData` and published;
      spectral check (G10), regenerating with void-and-cluster if it fails.
- [ ] Volumetric fog adopts `temporal_frame_index`, the shared sequences and
      the blue-noise texture; its private Halton is removed.
- [ ] Debug modes and statistics from §10.
- [ ] Volumetric fog composite: per-pixel blue-noise dither of the volume
      lookup depth, one slice deep, from the shared blue noise ([#20](https://github.com/abdes/DroidNet/issues/20)).
- [ ] **Tests.** G10; debug-mode image tests.
- [ ] Re-run the [#20](https://github.com/abdes/DroidNet/issues/20) acceptance in `CityEnvironmentValidation` with the
      resolve on. Comment the result on the issue; close it if every
      criterion passes, otherwise narrow it to what remains (grid depth
      distribution, shaft shape).

### S8 — Editor aids and stateless views

Design: LLD §8. Decisions TR-D11, TR-D12.

- [ ] Output closest depth; wireframe, ground grid, selection outline, view
      overlays and picking at output resolution, unjittered.
- [ ] Stateless views: spatial-only path; capture API takes a state handle
      and warm-up count.
- [ ] **Tests.** G11; stateless view allocates no history.
- [ ] **Owner.** Editor at 100 % and 50 %: grid, selection, picking and
      gizmos align with the image.

### S9 — Closeout

- [ ] Owner documents current; `CheckDocumentation.py` passes; `STATUS.md`
      regenerated.
- [ ] Close [#15](https://github.com/abdes/DroidNet/issues/15) with the gate evidence.
- [ ] Remove the VX-POST-02 row from OPEN_ITEMS; VX-SHADOW-01 can start.
