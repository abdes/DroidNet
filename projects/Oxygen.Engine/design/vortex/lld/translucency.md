# Translucency Stage LLD

**Phase:** 5C - Remaining Services
**Deliverable:** `VTX-M05C`
**Status:** `validated`

## Mandatory Vortex Rule

- `Oxygen.Renderer` is legacy dead code. It is not a reference
  implementation, fallback, compatibility layer, or shortcut for this work.
- Vortex translucency is a native SceneRenderer stage. Parity claims must be
  grounded in UE5.7 renderer/shader source under
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- If the implemented scope is narrower than UE5.7, the accepted Oxygen
  divergence must be documented here and in the detailed milestone plan before
  status can advance.

## 1. Goal

Implement Stage 18 standard forward-lit translucency for the production
desktop deferred baseline. The stage consumes the prepared scene, draws
alpha-blended transparent meshes with the forward lighting shader family, depth
tests against the deferred opaque depth buffer, and composites directly into
`SceneColor`.

The milestone is intentionally not a full UE translucency stack. It is the
minimal professional stage needed by Vortex now: ordinary alpha-blended meshes
that are lit, sorted, depth-tested, diagnosable, and visually provable.

## 2. UE5.7 Source Mapping

The implementation must be reviewed against these UE5.7 files before closure:

| UE5.7 area | Files | Oxygen decision |
| --- | --- | --- |
| Translucency pass orchestration | `Renderer/Private/TranslucentRendering.cpp`, `.h` | Oxygen implements the standard main translucency pass shape only. |
| Translucent render state | `Renderer/Private/BasePassRendering.cpp`, `.h` | Oxygen uses standard alpha blending over scene color and read-only scene depth. |
| Mesh-pass classification | `Renderer/Private/SceneVisibility.cpp`, `BasePassRendering.cpp` | Oxygen consumes prepared draws tagged with `PassMaskBit::kTransparent`. |
| Translucent sorting | `Renderer/Private/MeshDrawCommands.cpp`, `.h` | Oxygen starts with per-view back-to-front distance sorting; richer priority/axis policies are deferred. |
| Forward/base pass shader contracts | `Shaders/Private/BasePass*.usf`, `BasePassCommon.ush` | Oxygen uses the existing `Vortex/Stages/Translucency/ForwardMesh_*` shader family. |
| Separate translucency composition | `Shaders/Private/ComposeSeparateTranslucency.usf` | Deferred; M05C composites directly into `SceneColor`. |

Important UE behaviors used as the target:

- Translucent meshes are rendered after opaque/deferred lighting work.
- Standard translucency uses the scene depth buffer for depth testing and does
  not write depth for ordinary alpha-blended surfaces.
- Translucency is sorted back-to-front within a view before blending.
- UE has many pass families: standard, standard modulate, after-DOF,
  after-DOF modulate, after-motion-blur, holdout, separate translucency,
  sorted-pixels/OIT, distortion, and translucency lighting volumes.

M05C parity re-check result:

- `Renderer/Private/BasePassRendering.cpp` `SetTranslucentRenderState` maps
  ordinary translucent materials to straight alpha color blending. Oxygen uses
  `SrcAlpha` over `InvSrcAlpha`.
- UE's standard translucent pass processor uses depth testing with depth writes
  disabled. Oxygen binds `SceneDepth` read-only and disables depth writes.
- Oxygen's validation materials may use `MATERIAL_FLAG_UNLIT` to isolate color
  and blend proof. This is not a separate translucency mode; it is the existing
  material shading contract, now honored by the forward translucency shader.
- The UE re-check does not remove Oxygen's internal exposure-domain contract:
  Vortex Stage 18 writes linear HDR `SceneColor` before Stage 22 tonemap. Any
  constant-color/unlit validation material that is meant to preserve authored
  material color must be written in the same exposure domain consumed by the
  post-process path.
- Senior-review remediation on 2026-04-26 tightened M05C correctness without
  expanding the milestone into the full UE translucency stack: sparse prepared
  bounds now fall back to render-item world bounds before distance fallback,
  invalid translucent draw commands are rejected CPU-side, projection-kind
  detection uses the canonical projection matrix element, infrastructure skips
  carry explicit reasons, and the Stage 18 PSO descriptor/root bindings are
  cached per framebuffer/depth/reverse-Z key.
- Accepted M05C shader divergence: Oxygen still uses the shared
  `ForwardMesh_PS.hlsl` PBR/aerial-perspective path for translucency. UE's
  ordinary translucency has lighter-weight lighting paths and material fog
  controls. A translucency-specific lightweight shader permutation and
  per-material aerial-perspective/fog control remain deferred work, not hidden
  Stage 18 parity claims.

## 3. Oxygen Scope

### 3.1 In Scope For M05C

- `TranslucencyModule` as the Stage 18 owner.
- `TranslucencyMeshProcessor` that builds draw commands from the current
  `PreparedSceneFrame` transparent pass partition.
- Standard alpha-blended material domain support through
  `MaterialDomain::kAlphaBlended` and `PassMaskBit::kTransparent`.
- Forward-lit shading through the existing
  `Vortex/Stages/Translucency/ForwardMesh_VS.hlsl` and
  `ForwardMesh_PS.hlsl` shader entries.
- Direct composition into the active `SceneColor` render target.
- Read-only `SceneDepth` depth testing with no depth writes.
- Back-to-front sorting for transparent draws.
- Diagnostics pass/product facts sufficient for RenderDoc/proof scripts.
- A validation scene in `Examples/VortexBasic` with overlapping transparent
  meshes, opaque blockers, lit surfaces, and a floor that makes blending and
  depth testing visible.
- An authored post-process exposure setup for the validation scene. M05C proof
  must not rely on auto exposure, because auto exposure can hide whether the
  translucency shader or the post-process stage owns a color error.

### 3.2 Out Of Scope

- Separate translucency textures and post-DOF composition.
- Modulate, holdout, after-motion-blur, distortion, refraction, water, hair,
  translucent velocity, and transmission.
- Order-independent transparency or UE sorted-pixels OIT.
- Translucent shadow depth rendering and colored/transmittance shadows.
- Per-primitive translucent sort priority, distance offset, and sort axis.
- Translucency lighting volume injection. Oxygen's existing forward-lighting
  payload is used directly.
- Draw-command state merging into `DrawIndexedInstanced` buckets. Stage 18
  still emits per-mesh draw commands for M05C.
- Per-material sided rasterizer selection. M05C uses no culling for visual
  validation; material-authored one-sided translucent PSOs are deferred.

These are not hidden gaps in the M05C claim. They are future scope because the
current Vortex baseline needs correct standard alpha-blended meshes before the
larger UE translucency family is worth implementing.

## 4. Stage Position

| Position | Stage | Contract |
| --- | --- | --- |
| Predecessor | Stage 15/17 post-opaque environment work | Opaque scene color/depth are available. |
| This | Stage 18 - Translucency | Draw sorted forward-lit transparent meshes into `SceneColor`. |
| Successor | Stage 20+ overlays, resolve, post-process | Debug overlays and final output observe the composited scene. |

Stage 18 must be skipped in wireframe-only render mode because that mode is a
diagnostic geometry view, not an exposed/tonemapped scene render.

## 5. Runtime Contracts

### 5.1 File Layout

```text
src/Oxygen/Vortex/SceneRenderer/Stages/Translucency/
  TranslucencyModule.h
  TranslucencyModule.cpp
  TranslucencyMeshProcessor.h
  TranslucencyMeshProcessor.cpp
```

### 5.2 Module API

```cpp
namespace oxygen::vortex {

struct TranslucencyExecutionResult {
  bool requested = false;
  bool executed = false;
  std::uint32_t draw_count = 0;
  TranslucencySkipReason skip_reason = TranslucencySkipReason::kNotRequested;
};

class TranslucencyModule {
 public:
  explicit TranslucencyModule(Renderer& renderer);
  ~TranslucencyModule();

  auto Execute(RenderContext& ctx, SceneTextures& scene_textures)
      -> TranslucencyExecutionResult;
};

} // namespace oxygen::vortex
```

The module owns no cross-frame rendering policy. It may cache graphics
resources and helper objects, but visibility, sorting, and pass execution are
derived from the current view.

### 5.3 Inputs

| Source | Data | Purpose |
| --- | --- | --- |
| Stage 2 InitViews | `PreparedSceneFrame` | Transparent draw metadata, materials, bounds, geometry handles. |
| Renderer/Core | bindless scene buffers and view constants | Same draw contract used by Vortex base/forward shaders. |
| LightingService | `LightingFrameBindings` | Forward direct and positional light evaluation. |
| ShadowService | `ShadowFrameBindings` | Directional shadow sampling where the shader supports it. |
| EnvironmentLightingService | `EnvironmentFrameBindings` | Ambient, aerial-perspective, and environment terms currently exposed to forward shaders. |
| SceneTextures | `SceneColor` RTV | Alpha-blended destination. |
| SceneTextures | `SceneDepth` read-only DSV | Opaque depth test, no transparent depth write. |

### 5.4 Outputs

| Product | Producer | Notes |
| --- | --- | --- |
| `Vortex.SceneColor` | Stage 18 | Same texture, now containing opaque + standard translucency. |
| `Vortex.TranslucencyDrawCommands` | Diagnostics fact | Draw count and skip reason, not a new GPU product. |

No new scene texture is allocated in M05C.

## 6. Mesh Processing

`TranslucencyMeshProcessor` builds commands from the current prepared scene:

1. Accept only draws tagged with `PassMaskBit::kTransparent` and main-view
   visibility.
2. Reuse the existing prepared draw metadata: material handle, geometry handle,
   submesh range, draw index, and bounding sphere.
3. Reject invalid material/geometry ranges rather than generating dummy draws.
4. Sort accepted draws back-to-front for the active view.

Sorting policy:

- Preferred path: use the resolved view camera and prepared draw bounding
  spheres to compute a conservative far-side depth/distance key.
- Sparse-bounds fallback path: use `RenderItemData::world_bounding_sphere` and
  the same view-space key calculation before falling back to scalar distance.
- Last-resort fallback path: use `sqrt(RenderItemData::sort_distance2)`, not
  squared distance, so fallback keys remain in linear units.
- Stable ordering is required for equal keys.

This deliberately matches UE's requirement that translucent work is sorted for
blending, while deferring UE's richer `ETranslucentSortPolicy`, priority, axis,
and distance-offset controls until Oxygen has those authoring fields.

## 7. Render State

| State | M05C value |
| --- | --- |
| Color target | `SceneColor` |
| Depth target | `SceneDepth`, read-only |
| Depth test | Enabled |
| Depth write | Disabled |
| Blend | Standard straight-alpha: source alpha over destination |
| Color space | HDR/linear scene color, before final post-process |
| Rasterizer | No culling for the first stage implementation; material-sided policy is deferred and must select separate PSOs before claiming one-sided translucent parity. |

The pixel shader must emit straight alpha. It must not tonemap or output LDR
color when writing to internal `SceneColor`.

### 7.1 Exposure-Domain Contract

Stage 18 writes before Stage 22. Stage 22 applies the resolved post-process
exposure before tonemapping. Therefore Stage 18 shader output must be in the
same linear scene-color domain expected by Stage 22.

Rules:

- Lit translucency writes physical linear HDR lighting results and leaves final
  display exposure to Stage 22.
- Unlit/constant-color material output that is intended to preserve authored
  color through the post-process path must use inverse exposure compensation in
  HDR output mode: write `authored_linear_rgb / max(GetExposure(), epsilon)`.
  Stage 22 then multiplies by exposure and recovers the intended authored
  color before tonemapping.
- This mirrors the existing Vortex wireframe/debug-line exposure compensation
  pattern. It is not emissive cheating, not LDR output, and not an extra
  tonemap inside Stage 18.
- Validation scenes must author a deterministic post-process exposure volume.
  They must not depend on auto exposure when proving material color, blend
  contribution, or depth order.

## 8. Capability And Skip Policy

Stage 18 requires the same runtime substrate as the deferred scene path:

- `RendererCapability::kScenePreparation`
- `RendererCapability::kDeferredShading`
- `RendererCapability::kLightingData`

Shadowing and environment lighting are consumed when present, but absence of a
shadow/environment product must not crash the standard translucent pass. Missing
optional products produce weaker lighting, not fake product publication.

Skip rules:

- No prepared scene: skip and record a diagnostics fact.
- No transparent draws: skip and record zero draws.
- Wireframe-only render mode: skip.
- Missing required core capabilities: do not construct the module.

## 9. Diagnostics And Proof Surface

Stage 18 must record one compact diagnostics pass entry:

- pass name: `Vortex.Stage18.Translucency`
- inputs: `Vortex.PreparedSceneFrame`, `Vortex.SceneColor`,
  `Vortex.SceneDepth`, plus lighting/shadow/environment binding products when
  available
- output: `Vortex.SceneColor`
- facts: transparent draw count, publication validity, and skip reason

This is enough for the Diagnostics panel, capture manifest, RenderDoc analyzer,
and CDB/debug-layer workflow without adding a new diagnostics subsystem.

## 10. Validation Scene Requirements

The VortexBasic M05C scenario exists to prove the rendering contract, not to be
an attractive sample scene. It must remain deliberately unambiguous:

- one opaque cube and one opaque floor in the base pass;
- exactly two transparent Stage 18 draws for the proof path;
- a cyan sphere and magenta cylinder with distinct silhouettes and authored
  alpha low enough that the opaque cube/background remain visible through them;
- no emissive boost used to fake visibility;
- a deterministic authored post-process exposure volume;
- no ground grid during proof captures;
- the sphere placed clearly in front of the cube and high enough to avoid
  ambiguous overlap with the cylinder/floor.

## 11. Validation Requirements

M05C cannot be marked `validated` until the single VTX-M05C ledger row records:

- Changed engine files and scene/proof files.
- UE5.7 reference files checked.
- Focused build success.
- Focused tests for transparent filtering, sort order, no-depth-write behavior
  where practical, and diagnostics facts.
- ShaderBake/catalog validation if shader catalog or shader requests change.
- Runtime/capture proof from the improved VortexBasic translucency scene:
  Stage 18 exists, transparent draw count is nonzero, draw order is
  back-to-front, alpha blend is enabled, depth is read-only, visible cyan and
  magenta material-color pixels are detected after Stage 18, `SceneColor`
  changes from the pre-Stage-18 baseline, and D3D12 debug layer reports no
  errors.
- User visual confirmation that the validation scene shows translucent
  blending and depth occlusion correctly.

Closure evidence is recorded in
[`../IMPLEMENTATION_STATUS.md`](../IMPLEMENTATION_STATUS.md): focused
build/tests, CDB/D3D12 audit, RenderDoc analyzer proof, UE5.7 re-check, and
user visual confirmation all passed for the final validation scene.
