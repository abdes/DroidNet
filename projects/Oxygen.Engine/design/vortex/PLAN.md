# Vortex Renderer Plan

Status: `initial execution plan`

This document captures the implementation and migration plan for the Vortex
renderer. It assumes the stable architecture in
[ARCHITECTURE.md](./ARCHITECTURE.md) and the concrete solution shape in
[DESIGN.md](./DESIGN.md).

Related:

- [PRD.md](./PRD.md)
- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [DESIGN.md](./DESIGN.md)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md)

Reference:

- [vortex-initial-design.md](./vortex-initial-design.md) — initial migration
  sketch with V.0–V.7 slices

## 1. Delivery Strategy

Vortex is delivered as a clean-slate module that coexists with the legacy
`Oxygen.Renderer`. The legacy module stays intact and functional. Both modules
exist in the repo simultaneously. The legacy module is deprecated once Vortex
reaches feature parity and all examples/tests switch over.

Guiding principles:

- copy + adapt, not git-move
- preserve the production runtime path in the legacy module throughout migration
- build Vortex incrementally, one slice at a time
- each slice must compile and link cleanly before the next starts
- domain systems are vertical slices added after the core shell works
- avoid a "big bang" integration step

## 1.1 Parallelization and Shippable States

The plan has two sequential phases:

1. **Foundation (V.0–V.7):** substrate migration, strictly sequential, no
   parallelism. Each slice depends on the previous one.
2. **Capability services (V.8+):** subsystem build-up, partly parallelizable
   once the core shell exists.

Intermediate shippable states:

- after V.7: empty renderer shell compiles, links, and smoke-tests
- after V.9: SceneTextures + GBuffer base pass produce visible deferred output
- after V.10: deferred lighting produces lit scene
- after V.12: deferred lighting with shadows for basic lit scenes (environment/post-process follow in V.13–V.14)

## 2. Foundation Slices (V.0–V.7)

These slices are carried forward from the initial design document with minor
refinements. They migrate the architecture-neutral substrate into the Vortex
module.

### V.0: Scaffold

Create the directory tree, `CMakeLists.txt`, and `api_export.h`. The target
compiles with an empty source set.

Exit gate: `cmake --build` succeeds for `oxygen::vortex`.

Status: **done** (scaffold already created).

### V.1: Cross-Cutting Types

Copy the 14 architecture-neutral type headers (§4.6 of
vortex-initial-design.md) into `Vortex/Types/`. Mechanical changes only:
namespace, export macro, include paths.

Exit gate: full build passes.

### V.2: Upload, Resources, ScenePrep

Copy the three self-contained subsystems:

- `Upload/` (14 files)
- `Resources/` (7 files)
- `ScenePrep/` (15 files)

Same mechanical changes as V.1.

Exit gate: full build passes.

### V.3: Internal Utilities

Copy the 7 renderer-core internal files into `Vortex/Internal/`. Mechanical
changes only.

Exit gate: full build passes.

### V.4: Pass Base Classes

Copy the 3 pass base class files into `Vortex/Passes/`. Adapt to reference
Vortex's `RenderContext`.

Exit gate: full build passes.

### V.5: View Assembly and Composition Infrastructure

Copy the view-assembly and composition infrastructure into the Vortex module.
There is no `Pipeline/` directory; these files are distributed across Renderer
Core and SceneRenderer based on ownership.

Vocabulary types placed at the Vortex root (Renderer Core public API):

- `CompositionView.h` — per-view composition descriptor
- `RendererCapability.h` — capability-family bitmask vocabulary
- `RenderMode.h` — wireframe / solid / debug enum

Desktop scene policy types placed in `SceneRenderer/`:

- `DepthPrePassPolicy.h` — depth pre-pass mode enum

Internal view-assembly and composition infrastructure placed in `Internal/`
(Renderer Core private):

- `CompositionPlanner.h/.cpp`
- `CompositionViewImpl.h/.cpp`
- `ViewLifecycleService.h/.cpp`
- `FrameViewPacket.h/.cpp`

Internal frame-planning and scene configuration placed in
`SceneRenderer/Internal/`:

- `FramePlanBuilder.h/.cpp`
- `ViewRenderPlan.h`

Not carried over:

- `RenderingPipeline.h` — abstract pipeline base class is eliminated. Vortex
  has no pipeline abstraction. View assembly is a Renderer Core responsibility;
  desktop scene policy is a SceneRenderer responsibility.
- `PipelineFeature.h` — pipeline feature flags are folded into
  `RendererCapability` vocabulary or SceneRenderer configuration as
  appropriate.
- `PipelineSettings.h/.cpp` — settings are absorbed into SceneRenderer
  configuration.

Mechanical changes: namespace, export macros, include paths. Update all
internal references to remove `Pipeline/` include paths.

Exit gate: full build passes. No `Pipeline/` directory exists. View-assembly
and composition types compile and link from their new locations.

### V.6: Renderer Orchestrator

This is the only non-trivial foundation slice. Copy `Renderer.h/.cpp` and its
direct dependencies into the Vortex root.

Adaptation work:

1. Strip domain members (~60 variables → ~10 kept)
2. Strip domain orchestration from frame-loop methods
3. Strip domain public API
4. Strip domain console bindings
5. Clean up `RenderContext` pass-type registry
6. Update `FacadePresets` include paths
7. Add scene renderer dispatch hooks (initially no-ops)

Exit gate: full build passes. Vortex `Renderer` compiles with no domain systems.
Can be instantiated with empty `CapabilitySet` and run the frame loop.

### V.7: Smoke Test

Create `Test/Link_test.cpp` that:

1. Instantiates `vortex::Renderer` with empty capability set
2. Verifies engine module attachment
3. Verifies frame-loop methods execute without crashing

Exit gate: link test passes, full build passes.

## 3. Scene Renderer Slices (V.8–V.10)

These slices introduce the deferred-first desktop renderer on top of the
foundation shell.

### V.8: SceneTextures, SceneRenderer Shell, and Stage Module Infrastructure

1. Implement `SceneTextures` (allocation, resize, accessors, rebuild state
   transition).
2. Implement `SceneRenderer` shell with frame-phase hooks.
3. Create `SceneRenderer/Stages/` directory structure.
4. Define the stage module execution contract
   (`Execute(RenderContext&, SceneTextures&)`).
5. Wire the scene renderer into the Renderer's frame loop as a delegate.
6. All stage module and subsystem service pointers are null; all stages are
   no-ops.

Exit gate: build passes. Scene renderer is instantiated and dispatched.
`Stages/` directory exists. No visible output beyond what the empty Renderer
already produces.

### V.9: Depth Prepass + GBuffer Base Pass (Stage Modules)

1. Create `DepthPrepassModule` in `SceneRenderer/Stages/DepthPrepass/` with
   depth-only pass, mesh processor, and partial velocity output.
2. Create `BasePassModule` in `SceneRenderer/Stages/BasePass/` with GBuffer
   MRT output (GBufferA–D + emissive → SceneColor) and base-pass mesh
   processor.
3. Implement base pass material shader contract (GBufferOutput).
4. Wire `DepthPrepassModule::Execute` and `BasePassModule::Execute` into
   frame stages 3 and 9.
5. Wire `SceneTextures::RebuildWithGBuffers()` after base pass.

Exit gate: GBuffers contain valid material data. SceneColor contains emissive.
Visual validation: GBuffer debug visualization shows correct normals, base
color, roughness.

### V.10: Deferred Lighting

1. Implement fullscreen pass-per-light deferred lighting (directional, point,
   spot).
2. Implement deferred light pixel shader reading from GBuffer SRVs.
3. Wire into frame stage 12.
4. Implement scene color resolve (stage 21).

Exit gate: lit scene visible in SceneColor after deferred lighting. Visual
validation: correct diffuse + specular response for at least 1 directional
and 1 point light.

## 4. Subsystem Service Slices (V.11–V.16)

Each subsystem is a self-contained vertical slice with its own service, passes,
types, and internal helpers.

### V.11: LightingService + Forward Light Grid

1. Create `Lighting/LightingService.h/.cpp`.
2. Implement light grid build (stage 6).
3. Implement `ForwardLightData` shared product.
4. Move deferred lighting (from V.10) under `LightingService`.
5. Forward light data available for translucency consumers.

Exit gate: light grid produces valid clustered data. Deferred lighting uses
LightingService. Forward light data is queryable.

### V.12: ShadowService

1. Create `Shadows/ShadowService.h/.cpp`.
2. Implement shadow depth rendering (stage 8) — conventional shadow maps first.
3. Wire shadow data into deferred lighting.
4. VSM integration is deferred to a later slice.

Exit gate: shadow maps rendered. Deferred lighting applies shadow terms. Visual
validation: correct directional shadow on a ground plane.

### V.13: EnvironmentLightingService

1. Create `Environment/EnvironmentLightingService.h/.cpp`.
2. Implement atmosphere / sky rendering (stage 15).
3. Implement IBL data production.
4. Implement fog.
5. Keep stage 14 volumetrics reserved within the Environment family for later
   activation; do not split a separate service in the initial release.

Exit gate: sky renders. IBL ambient term visible. Fog applies. Visual
validation: scene with sky, ambient, and distance fog.

### V.14: PostProcessService

1. Create `PostProcess/PostProcessService.h/.cpp`.
2. Implement tonemap pass.
3. Implement auto-exposure.
4. Implement bloom (if straightforward to carry from legacy).

Exit gate: tonemapped output to screen. Visual validation: HDR → LDR output
with correct exposure.

### V.15: DiagnosticsService

1. Create `Diagnostics/DiagnosticsService.h/.cpp`.
2. Implement GPU debug overlay.
3. Implement ImGui panel infrastructure.
4. Port GPU timeline profiler.

Exit gate: diagnostics overlay renders. ImGui panels functional.

### V.16: Translucency (Stage Module)

1. Create `TranslucencyModule` in `SceneRenderer/Stages/Translucency/` with
   forward translucency passes consuming shared forward light data from
   `LightingService`.
2. Wire `TranslucencyModule::Execute` into frame stage 18.

Exit gate: translucent objects render correctly with forward lighting. Visual
validation: translucent object with correct lighting + blending over deferred
opaque background.

## 5. Integration and Migration Slices (V.17–V.19)

### V.17: Occlusion / HZB (Stage Module)

1. Create `OcclusionModule` in `SceneRenderer/Stages/Occlusion/` with HZB
   generation from depth prepass output, occlusion query batching/testing,
   and temporal HZB handoff.
2. Wire `OcclusionModule::Execute` into frame stage 5.

Exit gate: occlusion culling reduces draw calls in complex scenes.

### V.18: Example Migration (`Examples/Async`)

1. Port `Examples/Async` from legacy Renderer to Vortex (PRD §6.1.1).
2. Validate full runtime path end-to-end.
3. Confirm non-runtime facades work against Vortex substrate.
4. Verify behavior-preserving migration through stable engine/renderer seams.

Exit gate: `Examples/Async` runs with Vortex producing correct visual output
matching legacy reference. Migration uses no long-lived compatibility clutter.

### V.19: Legacy Deprecation

1. Mark `Oxygen.Renderer` as deprecated.
2. Port remaining examples and tests.
3. Remove `Oxygen.Renderer` from the build.

This is the final migration step. It should only happen after all examples and
tests pass with Vortex.

Exit gate: `Oxygen.Renderer` removed from build. All tests pass.

## 6. Dependency Map

### 6.1 Foundation (Sequential)

```
V.0 (scaffold)
 └─► V.1 (types)
      └─► V.2 (upload, resources, sceneprep)
           └─► V.3 (internal utilities)
                └─► V.4 (pass bases)
                     └─► V.5 (view assembly + composition infra)
                          └─► V.6 (renderer orchestrator)
                               └─► V.7 (smoke test)
```

### 6.2 Scene Renderer (Sequential)

```
V.7 (smoke test)
 └─► V.8 (scene textures + scene renderer shell)
      └─► V.9 (depth prepass + base pass)
           └─► V.10 (deferred lighting)
```

### 6.3 Subsystem Services (Partially Parallel)

After V.10, several subsystems can proceed in parallel:

```
V.10 (deferred lighting)
 ├─► V.11 (lighting service + light grid)
 │    └─► V.16 (translucency — needs forward light data)
 ├─► V.12 (shadow service)
 ├─► V.13 (environment lighting)
 ├─► V.14 (post-process)
 └─► V.15 (diagnostics)
```

V.11 must precede V.16 (translucency needs forward light data).
V.12, V.13, V.14, V.15 are independent of each other.

### 6.4 Integration (Sequential)

```
V.16 (translucency)
 └─► V.17 (occlusion / HZB)
      └─► V.18 (example migration)
           └─► V.19 (legacy deprecation)
```

## 7. Milestones

### M1. Empty Shell (V.0–V.7)

Exit criteria:

- Vortex module compiles and links
- Renderer instantiates with empty capability set
- Smoke test passes

### M2. Deferred Scene Renderer (V.8–V.10)

Exit criteria:

- SceneTextures allocates and provides GBuffer products
- GBuffer base pass writes valid material data
- Deferred lighting produces a lit scene
- At least 1 directional + 1 point light working

### M3. Feature Services (V.11–V.16)

Exit criteria:

- All 5 subsystem services implemented
- Translucency works with forward light data
- Post-process produces tonemapped output
- Diagnostics overlay renders

### M4. Production Ready (V.17–V.19)

Exit criteria:

- HZB/occlusion culling working
- At least one example fully ported
- Legacy renderer deprecated and removed

## 8. Verification Plan

### 8.1 Per-Slice Verification

Every slice must pass:

1. Full build succeeds (CMake + compile + link)
2. Existing tests do not regress
3. Slice-specific exit gate met

### 8.2 Visual Verification Checkpoints

| Checkpoint | When | What to verify |
| --- | --- | --- |
| GBuffer debug view | After V.9 | Normals, base color, roughness visualized correctly |
| Lit scene | After V.10 | Correct diffuse + specular from deferred lighting |
| Shadows | After V.12 | Directional shadow on ground plane |
| Sky + ambient | After V.13 | Atmosphere renders, IBL ambient visible |
| Tonemapped output | After V.14 | HDR → LDR with correct exposure |
| Translucency | After V.16 | Correct blending over deferred background |
| Full scene | After V.18 | Ported example matches legacy reference |

### 8.3 Architectural Verification

At each milestone:

- confirm scene renderer dispatches stages in correct order
- confirm subsystem services do not hold back-pointers to renderer
- confirm SceneTextures products are valid when consumed
- confirm non-runtime facades work against Vortex substrate (M2+)
- confirm capability gating correctly enables/disables subsystems

## 9. Risk Areas

1. **V.6 complexity.** Stripping the monolithic renderer to a clean shell is
   the highest-risk slice. The adaptation surface is large even after removing
   domain code.
2. **Shader migration.** Base pass and deferred lighting shaders are new code
   paths. There is no legacy deferred shader to adapt from.
3. **GBuffer format debugging.** Encoding/decoding GBuffer data correctly
   requires per-target visual validation.
4. **Cross-subsystem data flow.** Deferred lighting depends on GBuffer, depth,
   shadows, and IBL data all being correct and correctly bound. Any binding
   error produces subtle visual bugs.
5. **Forward light data shape.** The six-`float4` UE5-aligned record may need
   tuning for Oxygen's existing light vocabulary.

## 10. Mitigations

1. **V.6 risk:** Start from a diff between legacy `Renderer.h/.cpp` and the
   target. List every member/method/block to strip before touching code.
   Build incrementally.
2. **Shader risk:** Build GBuffer debug visualization (normal, roughness, etc.)
   in V.9 before attempting deferred lighting in V.10.
3. **GBuffer format risk:** Use known-good reference materials (solid color,
   known roughness) for initial validation.
4. **Data flow risk:** Implement RenderDoc-captured frame validation at each
   visual checkpoint. Use frame 10 as the baseline capture point.
5. **Light data risk:** Start with the simplest light scenario (1 directional)
   and add complexity incrementally.

## 11. Explicit Deferrals

Deferred beyond the initial Vortex release:

- virtual shadow maps (VSM integration in ShadowService)
- geometry virtualization (Nanite-equivalent, stage 4)
- GI / reflections (Lumen-equivalent, stage 13)
- volumetric fog / clouds (stage 14)
- light shaft bloom / translucency upscale (stage 20)
- single layer water (stage 16)
- MaterialCompositionService (stages 7 and 11: DBuffer decals, deferred
  decals, material classification)
- distortion / translucent velocities (stage 19)
- tiled or clustered deferred optimization
- GBufferE / GBufferF
- task-targeted global compositor
- public inter-view dependency DAGs
- mobile renderer path

## 12. Exit Criteria

The Vortex renderer plan is complete when:

1. The architecture in [ARCHITECTURE.md](./ARCHITECTURE.md) is reflected in
   the code: scene renderer, SceneTextures, subsystem services, frame ordering.
2. The design contracts in [DESIGN.md](./DESIGN.md) are present: GBuffer base
   pass, deferred lighting, subsystem service interfaces, shared forward light
   data.
3. The runtime path produces correct visual output for at least one ported
   example.
4. Non-runtime facades work against the Vortex substrate.
5. The legacy `Oxygen.Renderer` is deprecated and removed.
6. Reserved future slots exist in the frame structure for all 23 stages.
