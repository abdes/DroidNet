# VTX-M05C — Translucency stage

Status: `validated`

| Field     | Summary                                                                       |
| --------- | ----------------------------------------------------------------------------- |
| Outcome   | Forward-lit translucency and its runtime qualification.                       |
| Remaining | Extensions: [VX-FAMILY-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities). |
| Evidence  | [Validation record](validation.md)                                            |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M03, VTX-M04D.5, VTX-M05A, VTX-M05B

## Delivered scope

Stage 18 standard forward-lit translucency consuming prepared transparent draws plus lighting/shadow/environment publications. Focused build/tests, senior-review regression remediation, CDB/D3D12 audit, RenderDoc proof, UE5.7 re-check, and manual visual confirmation passed.

## Scope and acceptance

Scope:

- Stage 18 forward-lit translucency consuming published lighting, shadow, and
  environment bindings.
- Correct blending over deferred scene color/depth.
- Shader families and material-output contracts for translucent passes.
- Detailed implementation plan:
  [`plan/VTX-M05C-translucency-stage.md`](README.md).
- M05C is limited to standard alpha-blended translucency. Separate
  translucency, post-DOF, holdout/modulate, OIT, distortion/refraction, and
  translucent shadows are future scope.

**Status:** `validated`
**Milestone:** `VTX-M05C - Translucency Stage`
**Scope owner:** Vortex SceneRenderer Stage 18
**Primary LLD:** [../lld/translucency.md](../../lld/translucency.md)

## 1. Goal

Implement the standard forward-lit translucency pass for Vortex. The milestone
closes when alpha-blended transparent meshes are selected from prepared scene
data, sorted back-to-front, rendered with the forward translucency shader family
over deferred `SceneColor`, depth-tested against read-only `SceneDepth`, and
proven in a runtime scene.

This is the standard translucency stage, not the full UE translucency stack.
Separate translucency, post-DOF passes, distortion, holdout, OIT, translucent
velocity, and translucent shadow depth rendering are future work.

## 2. Implementation Policy

- Use the existing Vortex prepared-scene, bindless draw metadata, scene texture,
  lighting, shadow, environment, and diagnostics contracts.
- Do not add a second material system or compatibility path.
- Do not allocate separate translucency textures in M05C.
- Use the existing `ForwardMesh_VS.hlsl` and `ForwardMesh_PS.hlsl`
  translucency shaders unless validation proves a shader contract gap.
- Treat the first M05C runtime proof attempts as invalid. User validation
  exposed black translucent output and unclear/missing cylinder visibility, so
  the implementation must be re-reviewed before any closure claim.
- Before continuing proof work, re-check UE5.7 translucency source and shaders
  against the Oxygen implementation and record the source-to-implementation
  mapping in this plan or the LLD.
- Keep diagnostics compact: one Stage 18 pass record and one draw-command fact.
- Senior-review remediation is part of the M05C proof surface: sparse bounds,
  invalid draw rejection, projection-kind detection, infrastructure skip
  diagnostics, and reverse-Z PSO descriptor reuse must remain covered by
  focused tests before the milestone can keep `validated` status.

## 4. UE5.7 References

Each implementation slice must re-check the relevant UE5.7 family:

- `Renderer/Private/TranslucentRendering.cpp`, `.h`
  - `ShouldRenderTranslucency`
  - `RenderTranslucency`
  - `RenderTranslucencyInner`
  - standard versus separate/post-DOF pass orchestration
- `Renderer/Private/BasePassRendering.cpp`, `.h`
  - `SetTranslucentRenderState`
  - translucent base-pass render target/depth setup
  - mesh processor factories for translucent mesh passes
- `Renderer/Private/MeshDrawCommands.cpp`, `.h`
  - translucent sort policy, priority, and distance packing
- `Renderer/Private/SceneVisibility.cpp`
  - translucent primitive pass counting/classification
- `Shaders/Private/BasePassPixelShader.usf`,
  `BasePassCommon.ush`, and `ComposeSeparateTranslucency.usf`
  - shader contract and explicitly deferred separate-translucency composition

## 5. Non-Goals

- No `Oxygen.Renderer` fallback.
- No separate translucency, downsampled translucency, post-DOF composition, or
  holdout/modulate pass.
- No OIT/sorted-pixels implementation.
- No refraction/distortion/transmission/water/hair claim.
- No translucent shadow depth rendering or colored shadowing.
- No new renderer capability family for translucency unless later runtime
  variants require it.

## 6. Implementation Slices

### Slice A - Architecture And Plan Authority

**Status:** `validated`

Tasks:

- Update [../lld/translucency.md](../../lld/translucency.md) into the
  authoritative M05C design.
- Create this dedicated M05C plan.
- Update [../PLAN.md](../../PLAN.md) and the single VTX-M05C row in
  [Milestone record](../../PLAN.md).

[Checks](validation.md#slice-a---architecture-and-plan-authority--checks).

Remaining gap:

- No Slice A gap.

### Slice B - Mesh Processor

**Status:** `validated`

Tasks:

- Add `TranslucencyMeshProcessor`.
- Build draw commands from prepared metadata tagged with
  `PassMaskBit::kTransparent`.
- Preserve main-view visibility filtering.
- Sort transparent draws back-to-front for the current view with a stable
  fallback to render-item bounds and then linearized prepared distance.
- Reject invalid material handles, invalid bindless geometry slots, and
  zero-count geometry ranges before command creation.
- Add focused tests for filtering and sorting.

[Checks](validation.md#slice-b---mesh-processor--checks).

### Slice C - Stage 18 Module And Pipeline

**Status:** `validated`

Tasks:

- Add `TranslucencyModule`.
- Bind `SceneColor` as alpha-blended RTV and `SceneDepth` as read-only DSV.
- Create a forward translucency pipeline using
  `Vortex/Stages/Translucency/ForwardMesh_VS.hlsl` and
  `ForwardMesh_PS.hlsl` with HDR scene-color output.
- Use the same root/draw metadata contract as existing Vortex mesh passes.
- Cache root bindings and Stage 18 pipeline descriptors by scene-color format,
  depth format, sample count/quality, and reverse-Z state.
- Execute after opaque lighting/environment work and before overlays/resolve.

[Checks](validation.md#slice-c---stage-18-module-and-pipeline--checks).

### Slice D - SceneRenderer Diagnostics Integration

**Status:** `validated`

Tasks:

- Construct the module only when scene preparation, deferred shading, and
  lighting-data capabilities are present.
- Execute Stage 18 unless render mode is wireframe-only.
- Record `Vortex.Stage18.Translucency` pass facts and a compact
  `Vortex.TranslucencyDrawCommands` product/fact.
- Publish explicit skip reasons for no draws and infrastructure failures; log
  infrastructure failures rather than silently returning.
- Keep missing optional shadow/environment products nonfatal.

[Checks](validation.md#slice-d---scenerenderer-diagnostics-integration--checks).

### Slice E - VortexBasic Validation Scene

**Status:** `validated`

Tasks:

- Add a focused translucency scenario to VortexBasic, preferably behind an
  explicit proof option so existing occlusion/shadow proofs keep stable draw
  counts.
- Include at least two transparent meshes with overlap, an opaque blocker, and
  a lit floor/background so alpha blending and depth rejection are visible.
- Ensure objects are above the ground and the camera/light make the proof
  unambiguous.

[Checks](validation.md#slice-e---vortexbasic-validation-scene--checks).

### Slice F - Runtime Proof And Closeout

**Status:** `validated`

Tasks:

- Start from a clean proof baseline. Discard prior M05C capture/visual proof
  conclusions until the code review, UE5.7 parity review, and corrected
  VortexBasic scenario are complete.
- Run focused builds/tests.
- Run ShaderBake/catalog validation if shader requests changed.
- Run a CDB/D3D12 debug-layer audit.
- Capture the improved VortexBasic translucency scene.
- Analyze the capture for Stage 18 draw count, blend state, read-only depth,
  draw order, and final visible contribution.
- Pause after internal proof when visual validation has not yet been requested.
  Close only after manual visual confirmation is recorded.
- Record the outcome and supporting evidence in this milestone.

[Checks](validation.md#slice-f---runtime-proof-and-closeout--checks).

Internal proof evidence:

- UE5.7 re-check: `Renderer/Private/BasePassRendering.cpp`
  `SetTranslucentRenderState` uses standard straight-alpha blending for
  translucent materials; `CreateTranslucencyStandardPassProcessor` uses depth
  test with no depth write. Oxygen Stage 18 matches that shape for M05C.
- Shader contract correction: `ForwardMesh_PS.hlsl` now honors
  `MATERIAL_FLAG_UNLIT`, matching the material shading contract already used
  by the deferred GBuffer path.
- Validation-scene correction: VortexBasic uses no-texture, unlit foreground
  cyan sphere and magenta cylinder materials to isolate material color,
  alpha blending, depth testing, and draw ordering without light-angle or
  emissive washout ambiguity.
- Build/test proof: `cmake --build out\build-ninja --config Debug --target
Oxygen.Graphics.Direct3D12.ShaderBake
Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests
oxygen-examples-vortexbasic Oxygen.Vortex.SceneRendererDeferredCore
--parallel 4` passed; focused `ctest --preset test-debug -R
"Oxygen\.Vortex\.SceneRendererDeferredCore|Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog"
--output-on-failure` passed.
- Senior-review remediation proof on 2026-04-26: `cmake --build
out\build-ninja --config Debug --target
Oxygen.Vortex.SceneRendererDeferredCore --parallel 4` passed;
  `ctest --preset test-debug -R
"Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure` passed
  40/40 tests; `git diff --check` passed.
- Runtime proof: final artifacts under
  `out/build-ninja/analysis/vortex/translucency/m05c-final/` record CDB/D3D12
  `overall_verdict=pass`, `runtime_exit_code=0`, zero D3D12/DXGI errors,
  Stage 18 scope count 1, Stage 18 draw count 2, Stage 9 draw count 2,
  Stage 20 ground grid count 0, cyan pixels 2161, magenta pixels 225, and
  Stage 18 RGB delta 2682.43359.
- Fresh post-remediation runtime proof: artifacts under
  `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/`
  record CDB/D3D12 `overall_verdict=pass`, `runtime_exit_code=0`, no debugger
  break, zero D3D12/DXGI errors, zero blocking warnings, a runtime log with
  `Parsed with-translucency option = true` and repeated `Writing 4 draw
metadata` entries, and a RenderDoc translucency report with Stage 18 scope
  count 1, Stage 18 draw count 2, Stage 9 draw count 2, ground grid count 0,
  Stage 18 after post-opaque and before resolve, cyan pixels 2130, magenta
  pixels 225, `stage18_scene_color_changed=true`, and Stage 18 max RGB delta 2684.
- Manual visual confirmation: accepted after the final VortexBasic scene used
  the foreground cyan sphere and magenta cylinder, raised sphere, reduced
  alpha, and authored manual exposure.

Senior-review remediation evidence:

- Code changes: `TranslucencyMeshProcessor.cpp` now uses render-item world
  bounds before linear distance fallback, rejects invalid material/geometry
  draws, uses canonical projection-kind detection, and drops the incoherent
  submesh-as-LOD diagnostic fallback. `TranslucencyModule.cpp/.h` now records
  skip reasons, logs infrastructure skips, and caches root bindings plus
  pipeline descriptors. `SceneRenderer.cpp` publishes the skip reason in the
  Stage 18 diagnostics product.
- Added tests: sparse bounding-sphere fallback, projection-kind sort behavior,
  degenerate draw rejection, and reverse-Z pipeline descriptor hashes.
- Accepted deferred work: per-material sided culling, draw-command state
  merging/instancing, a lightweight translucency-specific pixel shader, and
  material-controlled aerial-perspective/fog application remain future scope
  and are documented in the LLD.

## 8. Exit Gate

M05C is `validated` because:

- Code review and UE5.7 parity review have been completed after the invalidated
  proof attempts.
- Stage 18 implementation exists and is wired into SceneRenderer.
- Transparent draw filtering and sorting are tested.
- The pass renders with alpha blending into `SceneColor` and read-only
  `SceneDepth`.
- Diagnostics/proof facts are exported.
- VortexBasic has a clear translucency validation scene.
- Focused build/tests pass.
- Runtime capture proof and D3D12 debug-layer audit pass.
- Manual visual validation confirmed the
  final validation scenario.
- `milestone README` records implementation files/areas, validation
  commands/results, UE5.7 references checked, and no hidden residual gap.

No M05C exit-gate gap remains.

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
