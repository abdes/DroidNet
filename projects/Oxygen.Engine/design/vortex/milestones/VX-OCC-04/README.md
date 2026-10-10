# VX-OCC-04 — Two-phase GPU occlusion

Status: `in_progress`

| Field     | Summary                                                                                                                  |
| --------- | ------------------------------------------------------------------------------------------------------------------------ |
| Outcome   | S2 validated. Design approved 2026-10-10.                                                                                |
| Remaining | S1–S7 below; tracked as [VX-OCC-04](../../OPEN_ITEMS.md#p2--engineering-follow-ups).                                     |
| Evidence  | Per-slice evidence in the [slice table](#slices); gates in [occlusion.md §8](../../lld/occlusion.md#8-validation-gates). |

The designs own every algorithm, format and gate:

- [occlusion.md](../../lld/occlusion.md)
- [hzb.md](../../lld/hzb.md)
- [geometry format §9.5](../../../content-pipeline/geometry-cooking-architecture.md#95-meshviewdesc-40-bytes-pakformat_geometryh)
- [mesh-view split](../../../../src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md#mesh-build-performed-by-meshbuildpipeline)

This plan owns only order, files, tests, owner actions and status. Paths
below are relative to `projects/Oxygen.Engine/src/Oxygen/`; shaders are
under `Graphics/Direct3D12/Shaders/Vortex/`.

## Resume

After a context compaction or in a new session:

1. Find the first slice in the [slice table](#slices) that is not
   `validated`.
2. Open its section and continue at the first unchecked box.
3. Run `git status` and `git log --oneline -8` to see uncommitted and landed
   work.
4. Re-read only the design sections the slice cites.

Update the slice table, the checkboxes and the header table as work lands.
Commit messages describe the change; they never cite this plan or its IDs.

## Decisions

Owner decisions, 2026-10-10:

| Decision                           | Value                                                                                                                                                |
| ---------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Culling granularity                | Mesh views from the cooker split (option A). Clusters are VX-OCC-05; mesh shaders are VX-OCC-06.                                                     |
| Shadow views                       | Two-phase occlusion per shadow view in this milestone.                                                                                               |
| Geometry v3 content                | Strict: v3 is rejected with the re-cook diagnostic. I re-cook test fixtures; the owner re-cooks editor projects and local content after S1.          |
| `vtx.occlusion.enable` at closeout | Default `true` for engine and editor; the CVar stays as a debug switch (S7).                                                                         |
| S4 equivalence proof               | Existing image-producing GPU suites pass unchanged, plus a new list-equivalence GPU test; the owner checks the editor visually before the S4 commit. |

## Working rules

- **Commits.** One thematic commit per slice once its tests pass; no review
  pause. A slice may land as more than one commit only when its parts build
  and pass independently. Work stops only at the **Owner** boxes.
- **GPU tests.** Run one GPU test process at a time, with `--gtest_filter` on
  the tests being worked on. A test that removes the D3D12 device is never
  rerun by me; the owner gets the command.
- **Builds.** I build engine targets and tests (Debug). The owner builds the
  SDK and the editor and runs the editor.
- **Before review.** Run `oxyformat` before building. Run `oxytidy` on changed
  C++ before asking for review; it reports zero warnings in those files.
- **Docs.** Update owner docs in the slice that changes the behavior they
  describe, not at closeout.

Commands, from `projects/Oxygen.Engine` after `vcvars64` (with the VS
Installer directory on `PATH`):

```text
cmake --build --preset oxygen-ninja-debug --target <targets>
ctest --preset oxygen-ninja-debug -R '^<test>$' --output-on-failure
./tools/cli/oxyrun.ps1 <test> -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=<case>
./tools/cli/oxyformat.ps1 <files> --fix
./tools/cli/oxytidy.ps1 <files> --configuration Debug --include-tests --build-dir out/build-ninja
python tools/vortex/CheckDocumentation.py
```

If a build reports a missing `impl-*.ninja`, reconfigure the tree with
`cmake --preset oxygen-ninja-default --fresh`, then
`./tools/build-tree.ps1 configure oxygen-ninja-default`.

## Slices

| ID  | Deliverable                                                  | Depends | State       | Commit      |
| --- | ------------------------------------------------------------ | ------- | ----------- | ----------- |
| S1  | Geometry v4: mesh-view bounds, cooker split, `MeshViewIndex` | —       | `planned`   |             |
| S2  | Tiled HZB build and occlusion pyramid                        | —       | `validated` | see git log |
| S3  | Culling records, history keys and slot allocator             | S1      | `planned`   |             |
| S4  | GPU indirect lists for camera passes, occlusion off          | S3      | `planned`   |             |
| S5  | Camera two-phase occlusion; old tester removed               | S2, S4  | `planned`   |             |
| S6  | Shadow-view lists and two-phase occlusion                    | S5      | `planned`   |             |
| S7  | Default on, capture gate, closeout                           | S6      | `planned`   |             |

S1 and S2 are independent; either may go first.

## S1 — Geometry v4

Design: [geometry §9.5](../../../content-pipeline/geometry-cooking-architecture.md#95-meshviewdesc-40-bytes-pakformat_geometryh),
[split](../../../../src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md#mesh-build-performed-by-meshbuildpipeline).

- [ ] **Data.**
  - Add `MeshViewIndex` to `Data/GeometryIndices.h`.
  - `Data/PakFormat_geometry.h`: `MeshViewDesc` grows to 40 bytes with local
    bounds.
  - `Data/PakFormatVersions.inc`: geometry version 4.
  - `Data/PakFormatSerioLoaders.h`
  - `Data/GeometryAsset.h/.cpp`: `MeshView` bounds accessor; views addressed
    by `MeshViewIndex`.
  - `Data/ProceduralMeshes.cpp`: views take the submesh bounds.
- [ ] **Loader.** `Content/Loaders/GeometryLoader.h` reads the view bounds and
      rejects bounds outside the submesh bounds. Version 3 fails with the existing
      re-cook diagnostic.
- [ ] **Model imports.** `Cooker/Import/Internal/Pipelines/MeshBuildPipeline.cpp`:
  - Morton-ordered split of static submeshes above 4096 triangles.
  - Tight view bounds.
  - Skinned and morphed submeshes stay one view.
- [ ] **Descriptor imports.**
  - `Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json`: optional
    `submesh_view.bounds`.
  - `Cooker/Import/Internal/GeometrySource.cpp`,
    `Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.cpp`: view bounds
    from JSON, else the submesh bounds.
  - `Cooker/Import/Internal/Pipelines/GeometryPipeline.cpp`: forwards the
    40-byte views.
- [ ] **Other `MeshViewDesc` readers.**
  - `Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
  - `Cooker/Tools/PakDump/GeometryAssetDumper.h`
  - `Cooker/Tools/Inspector/GeometryMetadata.cpp`
- [ ] **Fixtures that write descriptors.**
  - `Cooker/Test/Fixtures/DescriptorFixtures.h`
  - `Cooker/Test/Import/{PhysicsPhase3Closure,SceneDescriptorImportJob,InspectorGeometryMetadata}_test.cpp`
  - `Content/Test/{GeometryLoader,AssetLoader_generation}_test.cpp`
  - `Vortex/Test/ScenePrep/ScenePrepHelpers.h`
  - `Vortex/Test/Fixtures/GeometryUploaderTest.cpp`
  - `SceneSync/Test/RuntimeMotionProducerModule_test.cpp`
- [ ] **New tests.**
  - The split is deterministic, runs hold at most 4096 triangles, index and
    vertex ranges are contiguous, and bounds are tight.
  - Skinned meshes are not split.
  - Version 3 is rejected with the re-cook diagnostic.
  - A JSON view without bounds takes the submesh bounds.
  - Bounds outside the submesh are rejected.
- [ ] **Verify.** All `Oxygen.Data.*`, `Oxygen.Content.*` and `Oxygen.Cooker.*`
      suites pass, plus `Oxygen.Scene.*` and the Vortex CPU suites that load
      geometry.
- [ ] **Owner.** Re-cook editor projects and local content; confirm the editor
      loads them.

## S2 — Tiled HZB Build

Design: [hzb.md §4](../../lld/hzb.md#4-build-algorithm),
[§4.5](../../lld/hzb.md#45-occlusion-pyramid),
[§9.1](../../lld/hzb.md#91-unit--integration-proof).

- [x] **Shader.** Rewrite `Stages/Occlusion/ScreenHzbBuild.hlsl` as the
      tiled downsampler ([hzb.md §4.1](../../lld/hzb.md#41-overview)):
  - tile dispatch for mips 0-6, tail dispatch for mips 7-12
  - the padded 2:1 mip-0 mapping of hzb.md §4.3, unchanged
- [x] **Builder.** Factor the build into
      `Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.{h,cpp}`, shared by the
      published HZB and the occlusion pyramid and testable on its own:
  - inputs: a source depth SRV with its view rect, closest and/or furthest
    targets (R32F, full chain, one UAV per mip), and the extent and mip count
  - it owns the compute pipeline and a `PerViewStructuredPublisher` for pass
    constants (per-mip UAV indices for both pyramids, source rect, extents)
  - two `Dispatch` calls with a UAV barrier between them
  - mip 0 samples 2x2 at `origin + clamp(2 * texel + d, source - 1)`,
    closest = max, furthest = min
- [x] **Module.** `Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.{h,cpp}`:
  - per-mip UAVs on the history textures
  - remove the scratch textures and copies
  - `BuildOcclusionPyramid(ctx, recorder, source)`, stored per camera view;
    S6 adds the shadow-view key
- [x] **Catalog.** Register the shader changes in
      `Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`; ShaderBake passes.
- [x] **Graphics layer.** Confirm per-mip UAV views exist for 2D textures; add
      them only if they are missing.
- [x] **New GPU target.** Create `Oxygen.Vortex.Occlusion.Tests`
      (`gtest_program(... GPU ...)`, deps `oxygen-vortex-exposure-test-support`)
      in `Vortex/Test/CMakeLists.txt`.
- [x] **New GPU tests.** [hzb.md §9.1](../../lld/hzb.md#91-unit--integration-proof)
      items 6–7:
  - a single foreground pixel reaches every mip, for 1920 x 1080 and
    1366 x 768 sources
  - the occlusion pyramid matches a CPU reference and is never published
- [x] **Verify.** `Oxygen.Vortex.SceneRendererPublication.Tests`,
      `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, and the local-fog cases of
      `Oxygen.Vortex.Exposure.Tests` (`LocalFog_test`, an HZB consumer) pass.

## S3 — Records And History

Design: [occlusion.md §2](../../lld/occlusion.md#2-data-model).

- [ ] **Culling record.** `Vortex/Types/DrawCullRecord.h` plus the HLSL mirror
      `Contracts/Draw/DrawCullRecord.hlsli`, with layout static asserts.
- [ ] **Emitter.** `Vortex/Resources/DrawMetadataEmitter.{h,cpp}`:
  - one record per draw from the mesh view's local bounds
  - world AABB union for instanced batches
  - `kAlwaysVisible` for non-finite bounds
  - `cull_records_slot` in `DrawFrameBindings`, C++ and HLSL, filled in
    `Vortex/Renderer.cpp` (draw frame bindings)
- [ ] **Draw sources.** `Vortex/PreparedSceneFrame.h`: `DrawSource` gains
      `data::LodIndex` and `data::MeshViewIndex`.
- [ ] **Extraction.** `Vortex/ScenePrep/Extractors.h` and the emitter's view
      loop set them.
- [ ] **Slot allocator.**
      `Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.{h,cpp}`
      (per culling view, CPU only).
- [ ] **New tests.** [occlusion.md §8](../../lld/occlusion.md#8-validation-gates)
      gates 1–2, in `Oxygen.Vortex.OcclusionModule.Tests` and
      `Oxygen.Vortex.DrawMetadataEmitter.Tests`.
- [ ] **Verify.** `Oxygen.Vortex.ScenePrep.Tests`,
      `Oxygen.Vortex.DrawMetadataEmitter.Tests`,
      `Oxygen.Vortex.OcclusionModule.Tests`.

## S4 — GPU Lists, Occlusion Off

Design: [occlusion.md §3](../../lld/occlusion.md#3-indirect-lists), §4.1
(frustum and coverage only), [§4.6](../../lld/occlusion.md#46-rasterization-invariance),
[§5](../../lld/occlusion.md#5-consumers).

- [ ] **Cull kernel.** `Stages/Occlusion/OcclusionCull.hlsl`:
  - oriented-box frustum test and exact pixel-center coverage cull
  - writes the visibility bits; phase 1 = every in-frustum draw
- [ ] **Compaction.** `Stages/Occlusion/ListCompaction.hlsl`: predicate, two
      scan levels, scatter, count.
- [ ] **List builder.**
      `Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.{h,cpp}`:
  - per pass and bucket: candidate upload, args and count buffers, and the
    command signature (one root constant plus a draw)
  - issue through `CommandRecorder::ExecuteIndirect` with push constants and
    the count buffer
- [ ] **Consumers move to lists, keeping their CPU sorts:**
  - `Stages/DepthPrepass/DepthPrepassModule.cpp` and `DepthPrepassMeshProcessor.cpp`
  - `Stages/BasePass/BasePassModule.cpp`: deferred, forward, radiance replay,
    wireframe and velocity aux
  - `Stages/Translucency/TranslucencyModule.cpp`
  - `Shadows/Passes/ContactShadowCasterDepthPass.cpp`
- [ ] **Invariance.** One `precise` clip-position function shared by
      `Stages/DepthPrepass/DepthPrepass.hlsl` and the base-pass shaders.
- [ ] **New GPU tests** in `Oxygen.Vortex.Occlusion.Tests`:
  - the lists equal the previous CPU draw sets for a fixed scene
  - two runs produce identical lists
  - translucent lists keep back-to-front order
  - a coverage-culled draw renders no pixels
- [ ] **Verify.** `Oxygen.Vortex.Exposure.Tests` and
      `Oxygen.Vortex.LightingImageReference.Tests` pass unchanged, run one at a
      time; plus the CPU suites of the touched modules.
- [ ] **Docs.** [depth-prepass.md](../../lld/depth-prepass.md),
      [base-pass.md](../../lld/base-pass.md),
      [translucency.md](../../lld/translucency.md) switch to indirect lists.
- [ ] **Owner.** Visual check of the editor before commit.

## S5 — Camera Two-Phase Occlusion

Design: [occlusion.md §4](../../lld/occlusion.md#4-two-phase-algorithm),
[§6](../../lld/occlusion.md#6-policies), [§7](../../lld/occlusion.md#7-diagnostics).

- [ ] **Module.** Rewrite `Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.{h,cpp}`:
  - `BuildPhase1` and `BuildPhase2`, per-view state, history reset
  - GPU stats buffer read asynchronously
  - `OcclusionConfig.h` loses `max_candidate_count`
- [ ] **Box test.** `OcclusionCull.hlsl` gains phase 2: the §4.5 box test
      against the occlusion pyramid, and the history write.
- [ ] **Frame order.** `Vortex/SceneRenderer/SceneRenderer.cpp`
      (`RenderCurrentView`): phase 1, phase 1 depth, occlusion pyramid, phase 2,
      phase 2 depth, `PartialDepth`, then Stage 5 Screen HZB. Pass markers per §7.
- [ ] **Remove the old tester:**
  - its readback, candidate and result buffers
  - `Types/OcclusionFrameResults` and `Types/OcclusionStats.h` (replaced by the
    new stats)
  - `ctx.current_view.occlusion_results` and the base pass consumer
    (`BasePassMeshProcessor.cpp`)
  - `vtx.occlusion.max_candidate_count` in `Vortex/Renderer.cpp`
  - the old `OcclusionTest.hlsl`
- [ ] **Diagnostics.** `Vortex.OcclusionFrameResults` facts move to the new
      counters (capture manifest).
- [ ] **New GPU tests.** [occlusion.md §8](../../lld/occlusion.md#8-validation-gates)
      gates 3, 4 (with occlusion on), 6 and 7.
- [ ] **Verify.** `Oxygen.Vortex.OcclusionModule.Tests`, the occlusion GPU
      tests, `Oxygen.Vortex.SceneRendererDeferredCore.Tests` and
      `Oxygen.Vortex.SceneRendererPublication.Tests`. D3D12 debug layer clean for
      the occlusion path.
- [ ] **Docs.** ARCHITECTURE stage table rows 3 and 5.

## S6 — Shadow Views

Design: [occlusion.md §6.4](../../lld/occlusion.md#64-shadow-views).

- [ ] **Caster culling.** `Vortex/Shadows/ShadowService.cpp` builds per shadow
      view lists (cascade, projected local map, cube face). They replace
      `Shadows/Internal/ShadowCasterCulling.{h,cpp}`.
- [ ] **Passes.** `Shadows/Passes/{ShadowDepthPass,CascadeShadowPass}.cpp`
      draw phase 1, build the occlusion pyramid from shadow depth, draw phase 2.
- [ ] **History.** Keyed by light, cascade or face, and allocation
      generation. Reset on projection change. The occlusion pyramid storage in
      `ScreenHzbModule` takes the same key.
- [ ] **Bias.** The shadow-view bias exceeds the pass's largest
      rasterization depth bias.
- [ ] **Cache.** Cached local maps that are reused skip culling.
      `Shadows/Internal/ShadowCasterDependencies.cpp` keeps all light-frustum
      casters as dependencies.
- [ ] **Remove** the unused `Services/Shadows/Vsm/VsmInstanceCulling.hlsl` and
      `Services/Shadows/Conventional/ConventionalShadowCasterCulling.hlsl`.
- [ ] **New GPU tests.** [occlusion.md §8](../../lld/occlusion.md#8-validation-gates)
      gate 5:
  - shadow maps are texel-identical with occlusion on and off
  - a moving occluded caster invalidates a cached map
  - camera occlusion never removes a caster
- [ ] **Verify.** `Oxygen.Vortex.ShadowService.Tests`; the shadow cases of
      `Oxygen.Vortex.Exposure.Tests` and
      `Oxygen.Vortex.LightingImageReference.Tests`, one at a time.
- [ ] **Docs.** [shadow-service.md](../../lld/shadow-service.md) and
      ARCHITECTURE row 8.

## S7 — Closeout

- [ ] **Default on.** `vtx.occlusion.enable` defaults to `true`
      (`Vortex/Renderer.cpp`); the editor sets no override.
- [ ] **Gates.** [occlusion.md §8](../../lld/occlusion.md#8-validation-gates)
      gate 6 (chunked mesh) and gate 9 (capture: fewer base-pass and shadow draws,
      image identical to occlusion off).
- [ ] **Status.**
  - [occlusion.md](../../lld/occlusion.md) status and §6.1 reflect the
    delivered default.
  - The VX-OCC-04 row leaves [OPEN_ITEMS](../../OPEN_ITEMS.md).
  - This README becomes `validated`.
  - Regenerate STATUS with `CheckDocumentation.py --write-status`.
- [ ] **Owner.** Editor acceptance with occlusion on.
