# Camera-Relative Rendering LLD

**Phase:** renderer foundation
**Deliverable:** [VX-VIEW-03](../milestones/VX-VIEW-03/README.md)
**Status:** `planned` — decisions made; starts after Editor V0.1.

## Summary

Vortex renders in **translated world** space: absolute world positions minus
the view origin, which is the camera position. Every position a shader handles
is small near the camera, so float precision follows the distance to the
camera rather than the distance to the world origin. This is UE5.7's
convention (`PreViewTranslation`) and the numeric foundation for the temporal
resolve ([VX-POST-02](../OPEN_ITEMS.md#p3--unscheduled-capabilities)) and
virtual shadow maps ([VX-SHADOW-01](../milestones/VX-SHADOW-01/README.md)).

- The CPU builds every view matrix and every per-view record in double
  precision, subtracts the view origin, then converts to float.
- Object transforms stay view-independent. Each transform carries its
  absolute origin separately from its rotation and scale; the vertex shader
  subtracts the view origin from that origin before adding the rotated local
  position. One transform buffer serves every view.
- Shadow maps carry their own origin. Receivers move from the camera's
  translated space into the shadow's with one subtraction, so shadow records
  stay shareable across views.
- No absolute matrix or absolute camera position remains in the shader-visible
  view ABI. The few consumers that need absolute positions (height fog,
  atmosphere, world-aligned materials, editor grids) receive CPU-translated
  parameters or call explicit double-float helpers.

The scene graph keeps float transforms. The ABI already carries a double-float
origin, so a later double-precision scene
([abdes/DroidNet#49](https://github.com/abdes/DroidNet/issues/49)) changes no
shader contract.

## 1. Scope and Context

### 1.1 What This Covers

- The numeric model: spaces, the view origin, double-float values and the
  precision they guarantee (§3).
- CPU construction of views, per-view records and previous-frame data (§4).
- The GPU ABI: the world transform record, `ViewConstants`,
  `ViewHistoryFrameBindings`, culling records and the double-float shader
  library (§5).
- Every position consumer: mesh vertex shaders, culling, depth
  reconstruction, lighting, the light grid, conventional shadows, environment,
  materials, editor and debug primitives, and CPU visibility (§6–§9).
- Multi-view behavior, migration order, failure modes and validation gates
  (§10–§13).

Not covered: a double-precision scene graph (CR-D3), origin rebasing of scene
data, and physics precision.

### 1.2 Why Vortex Needs It

Every Vortex position is absolute float today: scene transforms, the view
matrix, `inverse_view_projection_matrix`, `camera_position`, light records,
shadow matrices and cull boxes
([inventory](../milestones/VX-VIEW-03/README.md#inventory)). A float holds 24
significant bits, so its spacing grows with magnitude:

| Distance from origin | Float spacing |
| -------------------: | ------------: |
|                 1 km |       0.06 mm |
|                10 km |       0.98 mm |
|               100 km |       7.81 mm |
|              1000 km |       62.5 mm |

Rasterization multiplies large absolute positions by a view matrix holding a
large translation, so vertex positions shimmer at the spacing of their
absolute coordinates. Depth reconstruction is worse: the inverse
view-projection mixes a large translation with depth precision, so deferred
lighting, fog and shadow lookups lose centimeters at city scale. Shadow
filtering, velocity and the temporal resolve depend on stable sub-pixel
positions; VSM page marking needs stable positions to keep cached pages valid.
Translated world space removes the large terms before any float arithmetic.

### 1.3 Authority

UE5.7 (`F:\Epic Games\UE_5.7\Engine`) is the reference:

| Topic                     | UE5.7 source                                                                                                                           |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| View origin and matrices  | `Source/Runtime/Engine/Private/SceneView.cpp:649-693` (`PreViewTranslation = -ViewOrigin`, rotation-only translated view)              |
| View uniform fill         | `SceneView.cpp:2662-2784` (double build, float cast; `ClipToPrevClip` with the translation delta)                                      |
| Double-float values       | `Shaders/Private/DoubleFloat.ush:5-16, 206-268, 511-546`; `Core/Public/Math/DoubleFloat.h:22-26`                                       |
| Object transforms         | `Engine/Public/PrimitiveUniformShaderParametersBuilder.h:322-374` (`PositionHigh` + relative matrix); `SceneData.ush:159-167`          |
| Vertex translation        | `VertexFactoryCommon.ush:114-132`; `DoubleFloat.ush:511-523` (`DFTransformLocalToTranslatedWorld`)                                     |
| Lights                    | `Engine/Private/Components/LightComponent.cpp:374-376` (per-view `TranslatedWorldPosition`, built in double)                           |
| Shadows                   | `Renderer/Private/ShadowRendering.h:305-324` (`PreShadowTranslation`); `ShadowDepthRendering.cpp:168`; `ShadowRendering.cpp:1775-1814` |
| Atmosphere and height fog | `Engine/Public/Rendering/SkyAtmosphereCommonData.cpp:271-330`; `Renderer/Private/FogRendering.cpp:257-271`                             |
| Velocity                  | `Shaders/Private/VelocityShader.usf:80-99`; previous position with `PrevPreViewTranslation`                                            |
| Culling                   | `Shaders/Private/Nanite/NaniteSceneCommon.ush:31-47` (per-view translation of shared transforms)                                       |
| Materials                 | `Engine/Public/Materials/MaterialExpressionWorldPosition.h:16-25` (absolute vs camera-relative world position)                         |

Vortex differs where its data model differs: its scene is float, so it stores
one float origin per transform instead of UE's per-primitive `PositionHigh`
plus per-instance relative matrices (§5.1).

### 1.4 Rejected Alternatives

| Alternative                                                  | Why rejected                                                                                                                                                                                   |
| ------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-view transform buffers translated on the CPU             | Uploads every transform once per view and makes the transform buffer view-dependent: breaks one shared GPU scene, cached VSM pages rendered for several views and a future GPU-resident scene. |
| Absolute float transforms with a translated view matrix only | The vertex shader still forms `world × local` at absolute magnitude, so the rounding this design removes stays.                                                                                |
| Tile plus offset positions (UE5.0 LWC tiles)                 | More fields and arithmetic than double-float for the same range; UE5.7 itself moved GPU-scene data to double-float.                                                                            |
| Origin rebasing (moving scene data near the origin)          | Rewrites scene, physics and gameplay positions, breaks persistent identities and still leaves several views needing different origins.                                                         |
| GPU double precision                                         | 1/16 to 1/64 rate on desktop GPUs and absent from rasterization.                                                                                                                               |

## 2. Decisions

Decided 2026-10-12; the milestone records them in the
[milestone](../milestones/VX-VIEW-03/README.md#decisions).

| ID     | Decision                                                                                                                                                                                                                                                                                                                                              | Status  |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- |
| CR-D1  | **Transform encoding.** A 64-byte `WorldTransform`: a float 3×4 rotation/scale with a translation remainder, plus the float absolute origin. View-independent; the vertex shader translates (§5.1).                                                                                                                                                   | Decided |
| CR-D2  | **View origin.** Each camera view's origin is its eye position, so the camera sits at translated `(0,0,0)`. Every shadow map has its own origin (§7).                                                                                                                                                                                                 | Decided |
| CR-D3  | **CPU precision.** View construction, per-view records, shadow setup, CPU visibility and picking run in double. The scene graph stays float; a double-precision scene is a separate, deferred change that needs no shader-contract change.                                                                                                            | Decided |
| CR-D4  | **Record rule.** Per-view records (lights, light volumes, fog volumes, atmosphere, height fog, editor primitives) are translated on the CPU. View-independent records (transforms, cull boxes, shadow maps, future GPU scene data) are absolute and translated on the GPU.                                                                            | Decided |
| CR-D5  | **Shadow numeric domain.** Shadow records map shadow-translated positions and carry their origin; receivers add `O_view − O_shadow`. Cross-view shadow sharing is preserved. VSM uses the same rule (VSM-D13). GPU-fitted cascades snap with CPU-computed double-precision remainders of the cascade origin, one per quarter-octave texel phase (§7). | Decided |
| CR-D6  | **Absolute-position consumers.** No shader reconstructs absolute positions in float. Height fog and atmosphere get CPU-translated heights and centers; world-aligned material functions use double-float helpers (§9).                                                                                                                                | Decided |
| CR-D7  | **No absolute view ABI.** `view_matrix`, `inverse_view_projection_matrix` and `camera_position` leave `ViewConstants`; translated replacements take their place (§5.2). No dual absolute/translated path exists at any point.                                                                                                                         | Decided |
| CR-D8  | **Previous frame.** Previous positions use the previous origin and previous matrices; camera-only reprojection matrices are built in double. `ViewHistoryFrameBindings` holds only previous-frame and reprojection values (§8).                                                                                                                       | Decided |
| CR-D9  | **Migration.** Contracts migrate one family per slice with the view origin fixed at zero, which keeps each slice image-identical; a final slice switches the origin to the eye (§11).                                                                                                                                                                 | Decided |
| CR-D10 | **Schedule.** After Editor V0.1, on the contracts VX-GPU-01 restructured; prerequisite of VX-POST-02 and VX-SHADOW-01.                                                                                                                                                                                                                                | Decided |

## 3. Spaces and Numeric Model

| Space                   | Definition                                           | Where it lives                           |
| ----------------------- | ---------------------------------------------------- | ---------------------------------------- |
| Absolute world `W`      | Scene world space                                    | CPU (float scene, double derived values) |
| View origin `O_v`       | The view's eye position, double on the CPU           | CPU; double-float on the GPU             |
| Translated world `T`    | `W − O_v`                                            | All camera-view shader math              |
| Shadow origin `O_s`     | Cascade center, light position or VSM clipmap origin | CPU; double-float in shadow records      |
| Shadow-translated `T_s` | `W − O_s`                                            | Shadow raster and shadow lookups         |
| Local                   | Mesh space                                           | Vertex input                             |

**Double-float.** A double-float value `(high, low)` stores `high =
float(x)` and `low = float(x − high)`, about 46 significant bits. With a float
scene, every absolute value is exactly a float and `low` is zero; the format
exists so a double scene changes values, not contracts.

**Translation.** `a − O` for an absolute float `a` and a double-float `O` is
`(a − O.high) − O.low`. When `a` and `O.high` are close, the first
subtraction is exact (Sterbenz), so a nearby position keeps full precision
regardless of its absolute magnitude. The operation is written once, in
`DFSubtractDemote` (§5.4), with `precise` arithmetic so the compiler cannot
reassociate it.

**Resulting precision.** Rendering precision near the camera depends only on
distance from the camera. Absolute placement precision is the float spacing
of the scene transform (§1.2 table): at 100 km, objects and the camera move
in 7.81 mm steps. That limit belongs to the float scene graph (CR-D3).

**Invariants.**

1. Every shader position is translated (`T` or `T_s`), a direction, or a
   double-float absolute value handled by §5.4 helpers.
2. A float absolute position is never formed on the GPU.
3. Every CPU value derived from the view origin is computed in double and
   converted to float once, at upload.
4. Previous-frame positions are relative to the previous origin; current
   positions to the current origin. Mixing them goes through a
   double-built matrix (§8).

## 4. CPU Construction

`ResolvedView` gains a double view origin and builds every matrix in double
from the float camera transform:

- `translated_world_to_view`: rotation only (the eye is the origin).
- `view_to_clip`: the projection; jittered when the temporal resolve is active
  ([VX-POST-02](../OPEN_ITEMS.md#p3--unscheduled-capabilities) owns jitter).
- `stable_view_to_clip`: the projection without jitter.
- `translated_world_to_clip`, `stable_translated_world_to_clip` and their
  inverses.

The float `ResolvedView` accessors for absolute matrices are removed with
their last consumer (CR-D7). `SceneCameraViewResolver` stops building
`glm::lookAt` with an absolute eye; it builds the rotation and records the
eye as the origin.

**Per-view records** (CR-D4) are built in double from the view origin:

| Record                                                      | Translated field                                                |
| ----------------------------------------------------------- | --------------------------------------------------------------- |
| `ForwardLocalLightRecord`                                   | `position_ws` becomes `translated_position`                     |
| Deferred light volume matrix (`DeferredLightPacketBuilder`) | `translate(position − O_v)`                                     |
| Light grid inputs                                           | `translated_world_to_view` replaces `view_matrix`               |
| Local fog volume instances and culling spheres              | translated position, translated culling sphere                  |
| Local fog tiled-culling frustum planes                      | planes of the translated frustum                                |
| Camera aerial perspective (`camera_planet_position_km`)     | camera position relative to the planet center, in double        |
| `EnvironmentViewData` (`BuildAtmosphereViewData`)           | translated planet center and camera altitude computed in double |
| Height fog                                                  | fog heights relative to `O_v.z` (§9)                            |
| Editor overlays and CPU-built debug geometry                | vertices translated per view                                    |

**CPU visibility.** Frustum, LOD distance and sort-distance tests run in
translated space: the frustum is built from the double translated matrices,
and bounds are moved into translated space in double before the float test.

**Picking.** `ViewPickPass` unprojects in double and returns the absolute
world position.

## 5. GPU ABI

The layouts below are entries in the generated contract schema
([VX-GPU-01](../milestones/VX-GPU-01/README.md) S8); C++ and HLSL mirrors are
generated. Records this design changes that are still hand-written move into
the schema in the slice that changes them: the light grid constants, deferred
light constants, `ForwardLocalLightRecord`, `EnvironmentViewData`, ground-grid
constants, local fog instances, `IblSkySnapshot` and the transform buffers
(`WorldTransform`).

### 5.1 World Transform Record

`WorldTransform` replaces the absolute `float4x4` in the current and previous
transform buffers. Size 64 bytes, unchanged.

| Offset | Field           | Meaning                                                                                                    |
| -----: | --------------- | ---------------------------------------------------------------------------------------------------------- |
|      0 | float4 `row0`   | Rotation/scale row 0 in `xyz`; translation remainder `x` in `w`                                            |
|     16 | float4 `row1`   | Row 1; remainder `y`                                                                                       |
|     32 | float4 `row2`   | Row 2; remainder `z`                                                                                       |
|     48 | float3 `origin` | Absolute translation, float (double-float high part)                                                       |
|     60 | uint `flags`    | Bit 0 `kWorldTransformUnchanged`: equals the previous-frame transform (set by VX-POST-02); other bits zero |

With the float scene, `origin` is the transform's translation and the
remainders are zero. A double scene stores `origin = float(t)` and the
remainder `float(t − origin)`.

The vertex shader computes:

```hlsl
precise float3 origin_t = DFSubtractDemote(xf.origin, view.view_origin);
precise float3 t = float3(dot(xf.row0.xyz, p) + xf.row0.w,
                          dot(xf.row1.xyz, p) + xf.row1.w,
                          dot(xf.row2.xyz, p) + xf.row2.w) + origin_t;
```

The normal-matrix buffer is unchanged: it holds no translation. The shared
mesh-position helper of the pass-input contract
(`Contracts/Draw/MeshPosition.hlsli`,
[shader-contracts §4.9](shader-contracts.md#49-pass-inputs-and-data-integrity))
is the only implementation. It takes the `WorldTransform`, the local position,
the world-space offset (material world-position offset, plus the motion-vector
world offset for velocity) and the double-float origin of the pass's view: the
camera origin, or the shadow origin for shadow passes. Offsets are directions,
so they add to `t` unchanged. Depth prepass, base pass, forward, velocity,
shadow and contact-shadow caster passes call it for current and previous
positions; it replaces the hand-written previous-position and shadow/forward
clip transforms in today's shaders. `ComputeMeshClipPosition` takes translated
matrices. The generic loader `BX_TryLoadInstanceWorldMatrix`
(`Core/Bindless/BindlessHelpers.hlsl`) returns `WorldTransform`.

### 5.2 ViewConstants

`ViewConstants` stays at `b1`, the bound-view header that only camera entry
points may read ([shader-contracts §4.9](shader-contracts.md#49-pass-inputs-and-data-integrity)), one versioned record per write:

| Offset | Field                                      | Meaning                                         |
| -----: | ------------------------------------------ | ----------------------------------------------- |
|      0 | uint64 `frame_seq_num`                     | Unchanged                                       |
|      8 | uint `frame_slot`                          | Unchanged                                       |
|     12 | float `time_seconds`                       | Unchanged                                       |
|     16 | float4x4 `translated_world_to_view`        | Rotation only                                   |
|     80 | float4x4 `view_to_clip`                    | Rasterization projection (jittered when active) |
|    144 | float4x4 `translated_world_to_clip`        | Rasterization, built in double                  |
|    208 | float4x4 `clip_to_translated_world`        | Inverse of the above, built in double           |
|    272 | float4x4 `stable_translated_world_to_clip` | Unjittered; velocity and reprojection           |
|    336 | float3 `view_origin_high`                  | Double-float view origin                        |
|    348 | uint `bindless_view_frame_bindings_slot`   | Unchanged                                       |
|    352 | float3 `view_origin_low`                   |                                                 |
|    364 | uint `reverse_z`                           | Unchanged                                       |
|    368 | uint `is_orthographic`                     | Unchanged                                       |
|    372 | uint3 reserved                             | Zero                                            |

Size 384 bytes. The camera is at translated `(0,0,0)`, so
`camera_position` is gone: the view direction of a perspective view is
`normalize(−T)`, and of an orthographic view the forward axis of
`translated_world_to_view`. VX-POST-02 adds its jitter and temporal fields to
this record in its own layout revision.

### 5.3 ViewHistoryFrameBindings

The history record holds previous-frame and reprojection values only; current
values come from `ViewConstants` (one source per value, VX-GPU-01 S4).

| Field                                                                 | Meaning                                                              |
| --------------------------------------------------------------------- | -------------------------------------------------------------------- |
| float4x4 `previous_translated_world_to_view`                          | Previous rotation                                                    |
| float4x4 `previous_stable_view_to_clip`                               | Previous unjittered projection                                       |
| float4x4 `previous_stable_translated_world_to_clip`                   | Previous frame, relative to the previous origin                      |
| float4x4 `translated_world_to_previous_clip`                          | Current `T` to previous unjittered clip, built in double (§8)        |
| float4x4 `stable_clip_to_previous_clip`                               | Current unjittered clip to previous unjittered clip, built in double |
| float3 `previous_view_origin_high`, float3 `previous_view_origin_low` | Previous double-float origin                                         |
| float2 `previous_pixel_jitter`                                        | Previous jitter (VX-POST-02)                                         |
| float4 `previous_view_rect_min_and_size`                              | Previous view rectangle                                              |
| uint `validity_flags`                                                 | `VIEW_HISTORY_FLAG_PREVIOUS_VIEW_VALID`                              |

The pass-input contract forbids substituting current values for an invalid
history; callers branch on the validity flag and apply their documented
behavior.

### 5.4 Double-Float Shader Library

`Shared/DoubleFloat.hlsli` holds the only double-float arithmetic:

| Function                                              | Use                                                   |
| ----------------------------------------------------- | ----------------------------------------------------- |
| `DFVector3 MakeDFVector3(high, low)`                  | Construct from a record                               |
| `float3 DFSubtractDemote(float3 a, DFVector3 o)`      | Absolute float position to translated                 |
| `float3 DFDifferenceDemote(DFVector3 a, DFVector3 b)` | Origin to origin, such as `O_view − O_shadow`         |
| `DFVector3 DFAdd(DFVector3 a, float3 t)`              | Translated back to absolute (two-sum), materials only |
| `float3 DFPeriodic(DFVector3 x, float period)`        | `x mod period` without forming `x` in float (§9)      |

All arithmetic is `precise`. A GPU unit test checks each against a CPU double
reference.

### 5.5 Non-Camera Inputs Records

Non-camera passes never bind `b1` ([shader-contracts §4.9](shader-contracts.md#49-pass-inputs-and-data-integrity)); each reads one inputs record captured
at record time. Every inputs record that carries a view or projection matrix
carries its translated form and the double-float origin it is relative to:

| Pass                                        | Translated inputs                                                                                                     |
| ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Contact-shadow caster depth                 | Camera origin, `translated_world_to_view`, `view_to_clip`                                                             |
| Shadow depth                                | Shadow origin, `shadow_translated_to_clip` (directional: the fitted cascade record), light position relative to `O_s` |
| Occlusion culling                           | The culling view's origin and translated clip matrix                                                                  |
| Light grid                                  | `translated_world_to_view`                                                                                            |
| Receiver fit and receiver mask              | Camera origin, `clip_to_translated_world`, translated view matrix, shadow origins                                     |
| Local fog tiled culling                     | Translated frustum planes                                                                                             |
| Camera aerial perspective                   | Camera position relative to the planet center                                                                         |
| LUT and capture passes with a view position | The capture origin                                                                                                    |
| Temporal resolve, VSM passes                | Per [temporal-resolve.md](temporal-resolve.md) and [VSM §5](virtual-shadow-maps.md#5-epoch-algorithm)                 |

The schema check (G6) covers inputs records as well as `b1`.

### 5.6 Culling Records

`OcclusionCull.hlsl` uses the view's `translated_world_to_clip` (or the
shadow's, §7). A local-space box goes through `WorldTransform` like a vertex.
A world-space box (`DRAW_CULL_WORLD_SPACE_BOX`) keeps an absolute float center
and is translated with `DFSubtractDemote`; its extent is unchanged. Draw-bound
debug boxes (`EmitDrawBounds`) are emitted in `T`.

## 6. Consumers

| Family                              | Files                                                                                                                                                                                                                              | Change                                                                                                      |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| Mesh vertex shaders                 | `BasePassGBuffer.hlsl`, `BasePassVelocityAux.hlsl`, `DepthPrepass.hlsl`, `ForwardMesh_VS.hlsl`, `DirectionalShadowDepth.hlsl`, `ContactShadowCasterDepth.hlsl`                                                                     | §5.1 helper; current and previous clip positions per §8                                                     |
| GPU culling                         | `OcclusionCull.hlsl`                                                                                                                                                                                                               | §5.6                                                                                                        |
| Depth reconstruction                | `Shared/PositionReconstruction.hlsli` and its callers in deferred lighting, IBL, fog, sky, atmosphere, local fog, volumetric fog and debug views                                                                                   | Reconstruct `T` with `clip_to_translated_world`                                                             |
| View vectors                        | `ResolveSurfaceViewDirection`, `DeferredShadingCommon.hlsli`, `DeferredLightDirectional/Point/Spot.hlsl`, `DeferredIbl.hlsl`, `ForwardMesh_PS.hlsl`, `ForwardMaterialEval.hlsli`, `ForwardDebug_PS.hlsl`, `BasePassDebugView.hlsl` | `−T` (perspective) or the view forward axis (orthographic)                                                  |
| Deferred and forward local lighting | `DeferredLightPoint.hlsl`, `DeferredLightSpot.hlsl`, `ForwardDirectLighting.hlsli`, `DeferredLightingCommon.hlsli`                                                                                                                 | Translated light positions and volume matrices (§4)                                                         |
| Light grid                          | `SpatialLightGrid.hlsl`                                                                                                                                                                                                            | `translated_world_to_view`                                                                                  |
| Conventional shadows                | `DirectionalShadowCommon.hlsli` (cascade, spot and point receiver math), shadow depth pass                                                                                                                                         | §7                                                                                                          |
| Contact shadows                     | `ContactShadow.hlsli`                                                                                                                                                                                                              | `translated_world_to_view`                                                                                  |
| Shadow surface normal               | `ShadowSurfaceNormal.hlsli`                                                                                                                                                                                                        | Derivatives of `T`; no change in code, the precision comes from the input                                   |
| Height and volumetric fog           | `Fog.hlsl`, `HeightFog.hlsli`, `VolumetricFog.hlsl`                                                                                                                                                                                | Translated fog heights (§9); froxel positions in `T`; reprojection with `translated_world_to_previous_clip` |
| Sky and atmosphere                  | `Sky.hlsl`, `AtmosphereCompose.hlsl`, `AerialPerspective.hlsli`, `AtmosphereCameraAerialPerspective.hlsl`, `EnvironmentViewData.hlsli`                                                                                             | Translated planet center; absolute planet-center field removed                                              |
| Local fog volumes                   | `LocalFogVolumeCommon.hlsli`, `LocalFogVolumeTiledCulling.hlsl`, `LocalFogVolumeCompose.hlsl`                                                                                                                                      | Culling spheres translated (positions already are)                                                          |
| Materials                           | `ForwardMaterialEval.hlsli`, `GBufferMaterialOutput.hlsli`                                                                                                                                                                         | Translated world position by default; `MakeStableGridPlane` uses `DFPeriodic` (§9)                          |
| Editor and debug                    | `GroundGrid.hlsl`, `ViewOverlay.hlsl`, `GpuDebugDraw.hlsl`, `GpuDebug.hlsli`, `GpuDebugView.hlsli`, selection outline and pick                                                                                                     | §9                                                                                                          |
| Conventional receiver analysis      | `ShadowReceiverCommon.hlsli`, `ConventionalShadowReceiverAnalysis.hlsl`, `ConventionalShadowReceiverMask.hlsl`                                                                                                                     | Reconstruct `T`; translucent receiver boxes through `WorldTransform`; bounds in shadow-translated space     |
| VSM                                 | `Services/Shadows/Vsm/*`                                                                                                                                                                                                           | Rewritten by VX-SHADOW-01 on this convention                                                                |

## 7. Shadows

A shadow map's origin `O_s` is chosen on the CPU in double:

| Shadow              | `O_s`                                                                                                                                                |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Directional cascade | The CPU cascade center, snapped in double absolute light space to the loose cascade's texel grid; the record also carries its texel-phase remainders |
| Spot                | The light position                                                                                                                                   |
| Point (each face)   | The light position                                                                                                                                   |
| VSM clipmap level   | The snapped clipmap origin ([VSM §3](virtual-shadow-maps.md#3-constants-and-coordinate-spaces))                                                      |

**Raster.** The shadow pass inputs record carries `O_s` and
`shadow_translated_to_clip`. The mesh-position helper translates by `O_s`
instead of `O_v`, so casters are rasterized at shadow-local precision.

**GPU-fitted cascades.** Receiver analysis
([shadow-service §5.4](shadow-service.md#54-receiver-analysis-and-receiver-mask-caster-culling))
fits each cascade on the GPU. It reconstructs receivers in `T`, moves them to
the cascade's `T_s`, fits and snaps the bounds in texel units of that space,
and writes `shadow_translated_to_clip` into the view's fitted
`ShadowCascadeBinding` records (`FittedCascades::records_srv`), which shadow
depth, caster culling and lighting read. The CPU bounds remain the
conservative outer bound for caster selection, also in `T_s`.

**Fitted-cascade snapping.** The fit picks its texel at run time: step `k`
gives `t_k = 2 · radius · 2^(−k/4) / resolution`, so every fitted texel is one
of four base sizes `t_j` (`j = 0..3`, the loose texel times `2^(−j/4)`)
divided by `2^⌊k/4⌋`. Snapping the fitted center `c` to a world-stable grid
needs `L mod t_k`, where `L` is the cascade origin in light space, and `L` is
too large for float far from the world origin. The CPU therefore computes, in
double, `r_j = L mod t_j` for the four base sizes and stores them in the
cascade record. Because `t_k` divides `t_(k mod 4)`,
`L mod t_k = r_(k mod 4) mod t_k`, a small float. The GPU snaps as

```hlsl
float r = fmod(record.texel_phase[k % 4], t_k);
float2 snapped = round((r + c) / t_k) * t_k - r;
```

which equals today's `round((L + c) / t_k) · t_k − L` without forming `L`.
The absolute `light_space_origin` field is removed. Snapping is exact and
world-stable at any distance.

**Records.** `VortexShadowCascadeBinding`, `ProjectedLocalShadowRecord` and
`CubeLocalShadowRecord` store `shadow_translated_to_clip` (per face for cubes)
and `O_s` as double-float, replacing `light_view_projection` and
`shadow_origin_ws`. Directional cascades are per view: built from the view
frustum and fitted to its receivers. Local records contain nothing
view-specific, so EX07's cross-view sharing and cache reuse are unaffected;
the sharing key (`LocalShadowRequest`) hashes `O_s` and the translated
matrices instead of absolute ones.

**Receivers.** A receiver at `T` computes once per shadow record:

```hlsl
float3 t_s = t + DFDifferenceDemote(view.view_origin, record.shadow_origin);
```

then applies the record's matrix. For point lights `t_s` is the
light-to-receiver vector used for the cube lookup and the distance compare.
Normal offsets and bias are applied in `t_s`.

## 8. Velocity and History

The vertex shader computes the previous position with the previous transform
and the previous origin, then projects with the previous frame's own matrix:

```hlsl
float3 t_prev = MeshTranslatedPosition(prev_xf, p, prev_world_offset,
                                       history.previous_view_origin);
float4 prev_clip = mul(history.previous_stable_translated_world_to_clip,
                       float4(t_prev, 1));
```

Each frame stays self-consistent, and the camera translation between frames
is never formed in float on the GPU. Screen-space consumers use the
double-built `stable_clip_to_previous_clip` (depth-only reprojection) and
`translated_world_to_previous_clip` (a current translated position to the
previous frame, used by volumetric fog). `PreviousViewHistoryCache` stores the
double origin with the previous matrices. A history discontinuity invalidates
the record exactly as today
([multi-view §9.3](multi-view-composition.md#93-persistent-per-view-resources)).

## 9. Absolute-Position Consumers

| Consumer                    | Treatment                                                                                                                                                                        |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Height fog                  | The CPU publishes each fog height relative to `O_v.z` in double; the shader evaluates `T.z − translated_height`. Camera-height terms come precomputed from the CPU, as in UE5.7. |
| Volumetric fog media        | Same translated heights for the media density.                                                                                                                                   |
| Atmosphere                  | `AtmosphereView` computes the translated planet center and camera altitude in double. Shaders use `T − planet_center_translated`.                                                |
| Sky capture                 | A capture is a view with its own origin at the capture point.                                                                                                                    |
| World-aligned materials     | The material contract offers the translated world position (default) and `GetAbsoluteWorldPosition()` as double-float. Periodic functions use `DFPeriodic`.                      |
| Procedural grid material    | `MakeStableGridPlane` uses `DFPeriodic(view_origin, period)` plus `T`, so lines stay world-stable at any distance.                                                               |
| Editor ground grid          | `GroundGridPass` computes its offset from the double origin; the shader stays in `T` and projects with translated matrices.                                                      |
| Editor overlays, debug draw | CPU-built geometry is translated per view on the CPU. GPU-emitted debug primitives are emitted in `T` by their producing shader.                                                 |

## 10. Multi-View

Each view has its own origin. Transform buffers, cull records and shadow
records are shared; per-view records are already published per view. A
borrowed or composed view (editor panes, captures) uses its own origin; no
product crosses views in translated form unless it carries its origin.

## 11. Migration (CR-D9)

With `O_v = 0`, translated space equals absolute space. Each slice migrates one
contract family completely, with the origin forced to zero, and must render
images identical to the previous slice within the gate tolerance. Absolute and
translated code never coexist for one family. The last migration slice sets
the origin to the eye and runs the precision gates (§13). Each family is
listed with its slice in the
[milestone plan](../milestones/VX-VIEW-03/README.md#slices).

## 12. Failure Modes and Diagnostics

| Failure                                              | Behavior                                                                                        |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| Non-finite view origin or matrices                   | The view is rejected with a logged reason before recording.                                     |
| A shadow or per-view record built without its origin | CPU check at publication; the view is rejected.                                                 |
| An absolute field reintroduced in the ABI            | The schema test (G6) fails the build.                                                           |
| Precision loss at extreme distance                   | Bounded by the float scene (§3); diagnostics show the absolute camera position and its spacing. |

A **precision** debug view shows `frac(T × 1000)` to make vertex and
reconstruction jitter visible during camera motion.

## 13. Validation Gates

| Gate | Check                                                                                                                                                                 |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G1   | Origin-zero equivalence: every migration slice renders the standard test scenes identically to its predecessor (depth exact; color within one 8-bit step).            |
| G2   | Offset invariance: a scene and camera moved by 10 km and 100 km on each axis render the same image, depth and velocity as at the origin, within tolerance.            |
| G3   | Static camera and static scene at 100 km: velocity exactly zero; shadow, fog and lighting stable over 300 frames.                                                     |
| G4   | Slow camera translation at 100 km: no vertex shimmer, no shadow shimmer, cascade texel snapping stable.                                                               |
| G5   | Two views with different origins sharing transforms and shadow records match their single-view renders.                                                               |
| G6   | The generated schema contains no absolute matrix or position in the view ABI; per-view records carry translated fields and shared records carry double-float origins. |
| G7   | CPU unit tests: double construction of view, reprojection and shadow matrices; GPU unit tests of every §5.4 function against a double reference.                      |
| G8   | Picking at 100 km returns the absolute position of the picked surface within the float spacing of that distance.                                                      |

## 14. Owner Documents

Each slice updates the documents for the contracts it migrates:
[shader-contracts](shader-contracts.md) (§4.6, §4.7, §5.2),
[lighting-gpu-abi](lighting-gpu-abi.md), [deferred-lighting](deferred-lighting.md),
[shadow-service](shadow-service.md), [environment-service](environment-service.md),
[init-views](init-views.md), [base-pass](base-pass.md),
[depth-prepass](depth-prepass.md), [occlusion](occlusion.md),
[editor-rendering](editor-rendering.md),
[runtime-motion-producers](runtime-motion-producers.md),
[multi-view-composition](multi-view-composition.md) and the numeric invariant in
[ARCHITECTURE](../ARCHITECTURE.md).
