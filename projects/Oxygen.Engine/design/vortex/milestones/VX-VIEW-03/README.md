# VX-VIEW-03 — Camera-relative rendering

Status: `planned`

| Field     | Summary                                                                                                         |
| --------- | --------------------------------------------------------------------------------------------------------------- |
| Outcome   | None yet. Every Vortex position is absolute float; this milestone moves the renderer to translated world space. |
| Remaining | Decisions recorded; S1–S9 remain. Starts after Editor V0.1.                                                     |
| Evidence  | Gates in [camera-relative-rendering.md §13](../../lld/camera-relative-rendering.md#13-validation-gates).        |
| Issue     | [abdes/DroidNet#58](https://github.com/abdes/DroidNet/issues/58)                                                |

The design owns the numeric model, every layout and every gate:
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

Decided 2026-10-12; rationale in [LLD §2](../../lld/camera-relative-rendering.md#2-decisions).

| Decision | Topic                       | Value                                                                                                             | Status  |
| -------- | --------------------------- | ----------------------------------------------------------------------------------------------------------------- | ------- |
| CR-D1    | Transform encoding          | 64-byte `WorldTransform`, view-independent, translated in the vertex shader                                       | decided |
| CR-D2    | View origin                 | Eye position per view; own origin per shadow map                                                                  | decided |
| CR-D3    | CPU precision               | Double for views and records; float scene kept; double scene [#49](https://github.com/abdes/DroidNet/issues/49)   | decided |
| CR-D4    | Record rule                 | Per-view records CPU-translated; shared records GPU-translated                                                    | decided |
| CR-D5    | Shadow numeric domain       | Shadow origin in records; receivers add `O_view − O_shadow`; fitted cascades snap with CPU texel-phase remainders | decided |
| CR-D6    | Absolute-position consumers | CPU-translated parameters or double-float helpers only                                                            | decided |
| CR-D7    | View ABI                    | No absolute fields; no dual path                                                                                  | decided |
| CR-D8    | Previous frame              | Previous origin; double-built reprojection; history holds previous only                                           | decided |
| CR-D9    | Migration                   | One family per slice at origin zero, then the origin switch                                                       | decided |
| CR-D10   | Schedule                    | After Editor V0.1, on the VX-GPU-01 contracts; before VX-POST-02 and VX-SHADOW-01                                 | decided |

## Inventory

Every spatial value is 32-bit float and absolute; there is no
double-precision position in Scene, Core or Vortex. C++ paths are under
`src/Oxygen/`; shader paths are under the Vortex shader root.

| Area                 | Facts                                                                                                                                                                                                                                                                                                                                                                |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scene                | `Scene/Detail/TransformComponent.h:53-55, 200` uses `glm::vec3`, `glm::quat` and `glm::mat4`.                                                                                                                                                                                                                                                                        |
| Transform upload     | `Vortex/ScenePrep/Extractors.h:76, 83-112` → `RigidTransformHistoryCache` → `Vortex/Resources/TransformUploader.cpp:36-43, 82-125, 247-260`: absolute `float4x4` worlds, previous worlds and normals in one per-frame array, appended per view. Shaders load them through `BX_TryLoadInstanceWorldMatrix` (`Core/Bindless/BindlessHelpers.hlsl:137-181`).            |
| View constants       | Schema entry `ViewConstantsGpuData` (`Vortex/Meta/ShaderContracts.yaml:41-69`, 256 B at `b1`): absolute `view_matrix`, `inverse_view_projection_matrix`, `camera_position`; built in float by `Vortex/Types/ViewConstants.h:158-172`, `Vortex/Renderer.cpp:3812-3820` and `Core/Types/ResolvedView.cpp:42-53`.                                                       |
| View history         | `ViewHistoryFrameBindings` (`ShaderContracts.yaml:116-124`) has no origin and keeps three current fields no shader reads (stable projection, jitter, view rect). `BasePassVelocityAux.hlsl:118-121` substitutes the current clip position when history is invalid.                                                                                                   |
| Mesh vertex shaders  | 6 entry points (`BasePassGBuffer`, `BasePassVelocityAux`, `DepthPrepass`, `ForwardMesh_VS`, `DirectionalShadowDepth`, `ContactShadowCasterDepth`) used by about 11 pipelines, including selection outline and view pick (`DepthPrepass`). Previous positions and the shadow and forward clip transforms are hand-written beside `Contracts/Draw/MeshPosition.hlsli`. |
| Depth reconstruction | 11 live shader files call `Shared/PositionReconstruction.hlsli`, more through `DeferredShadingCommon.hlsli`; 4 are unfinished VSM shaders. Conventional receivers reconstruct in `Services/Shadows/Conventional/ShadowReceiverCommon.hlsli:85-87`. CPU: `Vortex/Passes/ViewPickPass.cpp:266, 407`, `CascadeShadowSetup.cpp:83-87`.                                   |
| Lights               | `LightEvaluationRecords.cpp:202` absolute `position_ws`; `DeferredLightPacketBuilder.cpp:68-77` absolute volume matrix; `SpatialLightGrid.hlsl:65-69` absolute view matrix.                                                                                                                                                                                          |
| Shadows              | `CascadeShadowSetup.cpp:128-143, 276-323` snaps in absolute float; the GPU fit snaps again against the float `light_space_origin` (`ConventionalShadowReceiverAnalysis.hlsl:180-185`). Local records carry absolute `light_view_projection` and `shadow_origin_ws`; the sharing key hashes them (`LocalShadowRequest.cpp:50-69`).                                    |
| Environment          | `AtmosphereView.cpp:138-202` and `LocalFogVolumeState.cpp:127-154` already translate, in float, from the absolute camera position (`EnvironmentLightingService.cpp:483-490`); height fog uses the absolute camera height (`HeightFog.hlsli:57-107`); local fog tiled culling takes world-space frustum planes.                                                       |
| Materials and editor | `ForwardMaterialEval.hlsli:34-56` stable grid; `GroundGridPass.cpp:323-434` (the only positional double math, `dvec2`); `ViewOverlay.hlsl`, `GpuDebugDraw.hlsl` take absolute positions.                                                                                                                                                                             |
| Hand-written records | Not in the schema: `LightGridPassConstants`, `DeferredLightConstants`, `ForwardLocalLightRecord`, `EnvironmentViewData`, `GroundGridPassConstants`, local fog instances, `IblSkySnapshot`, the transform buffers.                                                                                                                                                    |

## Working rules

- **Commits.** One thematic commit per slice once its tests pass; no review
  pause. Work stops only at **Owner** boxes and for decisions I am not
  confident about, presented with facts and pros and cons.
- **Origin zero until S8.** Slices S2–S7 run with the view origin forced to
  zero and must pass gate G1 against the previous slice.
- **GPU tests.** One GPU test process at a time, filtered to the tests being
  worked on. A test that removes the D3D12 device is never rerun by me; the
  owner gets the command.
- **Builds.** I build engine targets and tests in Debug. The owner builds the
  SDK and the editor and runs the editor.
- **Contracts.** Layout changes go through the generated schema from
  VX-GPU-01 S8; no hand-written mirrors. A hand-written record a slice changes
  moves into the schema in that slice.
- **Issues.** Work deferred out of a slice becomes an abdes/DroidNet issue
  and an `OPEN_ITEMS` row in the same change. An issue this plan resolves is
  closed with its evidence when the resolving slice lands.
- **Docs.** Update the owner documents listed in
  [LLD §14](../../lld/camera-relative-rendering.md#14-owner-documents) in the
  slice that changes the contract they describe.

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

| Slice | Scope                                             | Depends on      | Status      |
| ----- | ------------------------------------------------- | --------------- | ----------- |
| S0    | Decisions                                         | —               | `validated` |
| S1    | Double view construction and double-float library | S0, Editor V0.1 | `planned`   |
| S2    | View ABI and depth reconstruction                 | S1              | `planned`   |
| S3    | World transforms, mesh shaders and culling        | S2              | `planned`   |
| S4    | Lighting records                                  | S2              | `planned`   |
| S5    | Shadows                                           | S3              | `planned`   |
| S6    | Environment                                       | S2              | `planned`   |
| S7    | Materials, editor and debug primitives            | S3              | `planned`   |
| S8    | Origin switch and precision gates                 | S3–S7           | `planned`   |
| S9    | Closeout                                          | S8              | `planned`   |

### S0 — Decisions

- [x] Decide CR-D1–CR-D10 (2026-10-12).
- [x] Record them in the LLD, this plan, OPEN_ITEMS, PLAN and the
      dependent plans (VX-POST-02, VX-SHADOW-01).

### S1 — Double view construction and double-float library

Design: LLD §3, §4, §5.4.

- [ ] `Core/Types/ResolvedView.{h,cpp}`: double view origin; translated,
      stable and inverse matrices built in double; float conversion once.
- [ ] `Vortex/SceneCameraViewResolver.cpp`: rotation-only view; the eye is the
      origin. The origin is forced to zero until S8.
- [ ] `Shared/DoubleFloat.hlsli` with every §5.4 function, `precise`
      throughout.
- [ ] Schema entries for `WorldTransform` and the new `ViewConstants` and
      `ViewHistoryFrameBindings` layouts, in a separate schema file entry with
      `Next`-suffixed names so `b1` keeps emitting the current cbuffer; S2
      replaces the current entries and drops the suffix.
- [ ] **Tests.** CPU: matrix construction against a double reference at
      0 / 10 km / 100 km. GPU: each double-float function against a CPU
      double reference (G7).

### S2 — View ABI and depth reconstruction

Design: LLD §5.2, §5.3, §6, §8.

- [ ] `ViewConstants` and `ViewHistoryFrameBindings` switch to the new layouts;
      `Renderer.cpp`, `Vortex/Types/ViewConstants.h` and
      `PreviousViewHistoryCache` (`CurrentState` stores the double origin)
      build them from `ResolvedView`. The unread current history fields go;
      `BasePassVelocityAux.hlsl` branches on the validity flag instead of
      substituting current values.
- [ ] `Shared/PositionReconstruction.hlsli` reconstructs `T`; migrate all
      reconstruction sites and view-vector sites in one change (deferred
      lighting, IBL, fog, sky, atmosphere, local fog, volumetric fog, debug
      views, forward debug, `ForwardMaterialEval`).
- [ ] Non-camera inputs records that carry view matrices take the translated
      forms and their origin (LLD §5.5): light grid, receiver fit
      (`ShadowReceiverFitInputs`, built at `SceneRenderer.cpp:2059-2069`) and
      mask, occlusion culling, local fog tiled culling, camera aerial
      perspective, LUT and capture passes.
- [ ] `SpatialLightGrid`, `ContactShadow.hlsli` and the shadow cascade depth
      selection use `translated_world_to_view`.
- [ ] Volumetric fog reprojects with `translated_world_to_previous_clip`.
- [ ] **Tests.** G1 against S1 on the standard scenes; CPU test that history
      records carry the previous origin and are invalidated by a
      discontinuity.

### S3 — World transforms, mesh shaders and culling

Design: LLD §5.1, §5.5, §5.6, §8.

- [ ] `Vortex/Resources/TransformUploader.{h,cpp}`: `WorldTransform` records
      for current and previous worlds; the transform buffers become schema
      entries.
- [ ] The shared mesh-position helper (`MeshPosition.hlsli`) takes
      `WorldTransform`, the world-space offsets and a double-float origin; all
      six vertex shaders compute current and previous positions through it.
      `BX_TryLoadInstanceWorldMatrix` returns `WorldTransform`; the receiver
      fit's translucent boxes (`ShadowReceiverCommon.hlsli:110-138`) read it.
- [ ] Contact-shadow caster inputs record: camera origin and translated
      matrices.
- [ ] `OcclusionCull.hlsl` and `DrawCullPass.cpp`: translated clip matrix
      (`OcclusionModule.cpp:245-252`); world-space boxes translated with
      `DFSubtractDemote`; draw-bound debug boxes in `T`.
- [ ] `Vortex/ScenePrep/Extractors.h`, `DepthPrepassMeshProcessor.cpp:60`,
      `TranslucencyMeshProcessor.cpp:45`: frustum, LOD and sort distances in
      translated space, computed in double.
- [ ] **Tests.** G1; a CPU visibility test at 100 km comparing translated and
      double reference culling.

### S4 — Lighting records

Design: LLD §4, §6.

- [ ] `LightEvaluationRecords.cpp`: `translated_position`;
      `DeferredLightPacketBuilder.cpp`: translated volume matrices;
      `DeferredLightPass.cpp`: CPU light frustum tests in translated space.
- [ ] `LightGridBuilder.cpp:220`, `SpatialLightGrid.cpp:411-420`,
      `LocalShadowQuality.h:69`, `ShadowEligibility.h:31`: CPU light tests in
      translated space.
- [ ] Point, spot and forward lighting shaders read translated positions.
- [ ] `LightGridPassConstants`, `DeferredLightConstants` and
      `ForwardLocalLightRecord` move into the schema.
- [ ] **Tests.** G1; light record unit tests at 100 km.

### S5 — Shadows

Design: LLD §7.

- [ ] `CascadeShadowSetup.cpp`, `SpotShadowSetup.cpp`, `PointShadowSetup.cpp`:
      shadow origins and matrices in double; cascade snapping in double
      absolute light space; the four texel-phase remainders computed in
      double into the cascade record.
- [ ] Shadow records carry `shadow_translated_to_clip` and a double-float
      origin; the shadow pass inputs record carries the origin for raster and
      the light position relative to it (`ShadowDepthPass.cpp`,
      `CascadeShadowPass.cpp`).
- [ ] `LocalShadowRequest.cpp`: the sharing key hashes `O_s` and translated
      matrices.
- [ ] `DirectionalShadowCommon.hlsli`: cascade, spot and point receivers
      translate with `DFDifferenceDemote`.
- [ ] `ConventionalShadowReceiverAnalysis.hlsl` fits in `T_s` and snaps with
      the texel-phase remainders (LLD §7); `light_space_origin` is removed;
      the fitted `ShadowCascadeBinding` records hold
      `shadow_translated_to_clip`.
- [ ] **Tests.** G1; cross-view shadow sharing still matches (EX07 tests);
      snapping stability at 100 km for every fit step `k`: the snapped grid
      matches a CPU double reference and does not move under camera motion.

### S6 — Environment

Design: LLD §9.

- [ ] `AtmosphereView.cpp` and its caller `EnvironmentLightingService.cpp`:
      translated planet center and altitude in double; remove the absolute
      planet-center field. Camera aerial perspective takes the camera position
      relative to the planet center.
- [ ] Height fog and volumetric fog: translated fog heights from the CPU.
- [ ] `LocalFogVolumeState.cpp`: translated culling spheres;
      `LocalFogVolumeTiledCullingPass.cpp`: translated frustum planes.
- [ ] Sky capture views use the capture origin (`CapturedSkySource.cpp`,
      `IblGpuProcessor.cpp`, `IblProcessing.hlsl`).
- [ ] `EnvironmentViewData`, local fog instances and `IblSkySnapshot` move
      into the schema.
- [ ] **Tests.** G1; fog and atmosphere unit tests for translated heights at
      high altitude.

### S7 — Materials, editor and debug primitives

Design: LLD §9.

- [ ] Material contract: translated world position by default;
      `GetAbsoluteWorldPosition()` as double-float.
- [ ] `ForwardMaterialEval.hlsli`: `MakeStableGridPlane` with `DFPeriodic`.
- [ ] `GroundGridPass.cpp`, `GroundGrid.hlsl`: double origin, translated
      matrices; `GroundGridPassConstants` moves into the schema.
- [ ] `ViewOverlay`, CPU-built debug geometry: translated per view on the
      CPU. GPU debug primitives (`GpuDebug.hlsli`, `GpuDebugView.hlsli`) are
      emitted in `T`.
- [ ] `ViewPickPass.cpp`: unprojection in double, absolute result.
- [ ] **Tests.** G1; G8; procedural-grid stability test at 100 km.

### S8 — Origin switch and precision gates

Design: LLD §11, §13.

- [ ] Set each view's origin to its eye position.
- [ ] Precision debug view (`frac(T × 1000)`).
- [ ] ARCHITECTURE: the numeric-domain invariant.
- [ ] **Tests.** G2, G3, G4, G5, G6 (schema check).
- [ ] **Owner.** Editor scene at 10 km and 100 km offsets: camera motion,
      shadows, fog and the ground grid are stable.

### S9 — Closeout

- [ ] Owner documents current per LLD §14; `CheckDocumentation.py` passes;
      `STATUS.md` regenerated.
- [ ] Remove the VX-VIEW-03 row from OPEN_ITEMS; VX-POST-02 can start.
