# VTX-M05C Translucency Stage

**Status:** `validated`
**Milestone:** `VTX-M05C - Translucency Stage`
**Scope owner:** Vortex SceneRenderer Stage 18
**Primary LLD:** [../lld/translucency.md](../lld/translucency.md)

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
- Keep the single VTX-M05C ledger row in
  [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) updated in place.

## 3. Current State

| Area | Current state | M05C action |
| --- | --- | --- |
| Material partitioning | `PassMaskBit::kTransparent` exists and draw metadata tests cover alpha-blended pass masks. | Consume the transparent partition in a Stage 18 mesh processor. |
| Shader family | `Vortex/Stages/Translucency/ForwardMesh_*` shaders are cataloged. | Create the pipeline using those shaders and HDR scene-color output. |
| Stage directory | Translucency directories contain placeholders only. | Add `TranslucencyModule` and `TranslucencyMeshProcessor`. |
| SceneRenderer | Stage 18 is a comment between environment/fog and overlays. | Construct and execute the module with diagnostics facts. |
| VortexBasic | It proves opaque, shadows, fog, occlusion, and diagnostics scenarios. | Add a focused translucency validation scene option with visible blend/depth cases. |

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

- Update [../lld/translucency.md](../lld/translucency.md) into the
  authoritative M05C design.
- Create this dedicated M05C plan.
- Update [../PLAN.md](../PLAN.md) and the single VTX-M05C row in
  [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

Validation:

- `git diff --check`.
- Consistency scan for stale M05C status/plan references.

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
  fallback to prepared `sort_distance2`.
- Add focused tests for filtering and sorting.

Validation:

- Focused Vortex test target build.
- Focused unit tests for transparent filtering and stable back-to-front order.

### Slice C - Stage 18 Module And Pipeline

**Status:** `validated`

Tasks:

- Add `TranslucencyModule`.
- Bind `SceneColor` as alpha-blended RTV and `SceneDepth` as read-only DSV.
- Create a forward translucency pipeline using
  `Vortex/Stages/Translucency/ForwardMesh_VS.hlsl` and
  `ForwardMesh_PS.hlsl` with HDR scene-color output.
- Use the same root/draw metadata contract as existing Vortex mesh passes.
- Execute after opaque lighting/environment work and before overlays/resolve.

Validation:

- Focused Vortex build.
- Tests or assertions for result reporting and no-draw behavior.
- ShaderBake/catalog validation only if shader catalog or shader requests
  change.

### Slice D - SceneRenderer Diagnostics Integration

**Status:** `validated`

Tasks:

- Construct the module only when scene preparation, deferred shading, and
  lighting-data capabilities are present.
- Execute Stage 18 unless render mode is wireframe-only.
- Record `Vortex.Stage18.Translucency` pass facts and a compact
  `Vortex.TranslucencyDrawCommands` product/fact.
- Keep missing optional shadow/environment products nonfatal.

Validation:

- Focused `SceneRendererPublication` or diagnostics tests where practical.
- Runtime capture manifest or RenderDoc analyzer proof of Stage 18 facts.

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

Validation:

- Build `oxygen-examples-vortexbasic`.
- Capture a scenario that shows the translucent Stage 18 draw calls.

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
  Close only after user visual confirmation is recorded.
- Update the single VTX-M05C ledger row with concise evidence and residual
  gap.

Validation:

- Focused build/test commands recorded in the ledger.
- Debug-layer report records zero D3D12/DXGI errors.
- RenderDoc analyzer report records Stage 18 proof facts.
- User visual confirmation is recorded before status becomes `validated`.

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
- Runtime proof: final artifacts under
  `out/build-ninja/analysis/vortex/translucency/m05c-final/` record CDB/D3D12
  `overall_verdict=pass`, `runtime_exit_code=0`, zero D3D12/DXGI errors,
  Stage 18 scope count 1, Stage 18 draw count 2, Stage 9 draw count 2,
  Stage 20 ground grid count 0, cyan pixels 2161, magenta pixels 225, and
  Stage 18 RGB delta 2682.43359.
- User visual confirmation: accepted after the final VortexBasic scene used
  the foreground cyan sphere and magenta cylinder, raised sphere, reduced
  alpha, and authored manual exposure.

## 7. Expected Test Commands

Initial focused commands:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore oxygen-examples-vortexbasic --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure
git diff --check
```

If shader catalog or shader request metadata changes:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Graphics.Direct3D12.ShaderBake Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure
```

Runtime proof artifacts live under:

```text
out/build-ninja/analysis/vortex/translucency/m05c-final/
  vortexbasic-translucency-m05c-final.debug-layer.report.txt
  vortexbasic-translucency-m05c-final_capture.rdc
  vortexbasic-translucency-m05c-final_capture.rdc_vortex_translucency_report.txt
```

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
- The user explicitly instructed visual validation and visually confirmed the
  final validation scenario.
- `IMPLEMENTATION_STATUS.md` records implementation files/areas, validation
  commands/results, UE5.7 references checked, and no hidden residual gap.

No M05C exit-gate gap remains.
