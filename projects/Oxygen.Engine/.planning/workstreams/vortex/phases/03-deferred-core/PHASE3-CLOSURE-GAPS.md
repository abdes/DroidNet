# Phase 3 Closure Gaps

This document tracks the full Phase 3 closure review findings gathered from
parallel read-only review lanes covering:

- architecture / PRD / PLAN parity
- implementation-status / planning truth
- LLD / runtime contract parity
- validation / tooling parity

Each finding is numbered and tracked with a checkbox.

Scoped closure note:

- Phase 03 items 6, 7, and 8 are accepted as closed for the maximum
  functional extent possible with the current engine features.
- Skinned / morph engine-enablement work remains explicit TODO carry-forward
  work and is no longer treated as a Phase 03 closure blocker.

Classification values are preserved exactly as requested:

- `genuine drift that happens during project implementations`
- `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
- `USER did a bad job expressing requirements and design`

## Findings

1. [x] **Phase 3 closure / verification surface is internally contradictory and stale**

   - Severity: `Critical`
   - Direction: `both`
   - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
   - Problem:
     - `design/vortex/IMPLEMENTATION-STATUS.md:3` says Phase 3 is blocked.
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md:4` still says `passed`.
     - `tools/vortex/AnalyzeDeferredCoreCapture.py` and `tools/vortex/Verify-DeferredCoreCloseout.ps1` still enforce the obsolete stencil-local-light closeout model.
   - Doc refs:
     - `design/vortex/IMPLEMENTATION-STATUS.md:3`
     - `design/vortex/IMPLEMENTATION-STATUS.md:73`
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md:4`
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md:29`
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md:30`
   - Code / tooling refs:
     - `tools/vortex/AnalyzeDeferredCoreCapture.py:317`
     - `tools/vortex/Verify-DeferredCoreCloseout.ps1`
   - Exact remediation:
     - Remove `03-VERIFICATION.md` as a current truth surface, or rewrite it so it describes the current runtime-based Phase 3 gate.
     - Demote the old frame-10 closeout scripts to `legacy` / `historical` status in docs and comments.
     - Remove any remaining “Phase 3 passed” wording that conflicts with the live runtime validation path.
     - Make `IMPLEMENTATION-STATUS.md`, `03-VALIDATION.md`, and the active validation scripts all tell the same closure story.
   - Remediation status:
     - `03-VERIFICATION.md` rewritten as a superseded historical record.
     - `AnalyzeDeferredCoreCapture.py` and `Verify-DeferredCoreCloseout.ps1` explicitly demoted to the historical 03-15 frame-10 closeout pack.
     - `IMPLEMENTATION-STATUS.md` no longer presents the historical frame-10 failure as the current authoritative closure verdict.

2. [x] **Runtime validation ownership is split between old plan text and the current toolchain**

   - Severity: `High`
   - Direction: `docs->code`
   - Classification: `genuine drift that happens during project implementations`
   - Problem:
     - `design/vortex/PLAN.md` still defers runtime capture validation to Phase 4.
     - Current Phase 3 validation uses live `VortexBasic` runtime proof in Phase 3.
   - Doc refs:
     - `design/vortex/PLAN.md:958`
     - `design/vortex/PLAN.md:963`
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md:48`
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md:68`
   - Code / tooling refs:
     - `tools/vortex/Run-VortexBasicRuntimeValidation.ps1:1`
     - `tools/vortex/Verify-VortexBasicRuntimeProof.ps1:1`
     - `tools/vortex/README.md:7`
   - Exact remediation:
     - Update `design/vortex/PLAN.md` so Phase 3 explicitly owns the `VortexBasic` runtime capture gate.
     - Remove or mark superseded any Phase 4-deferral text that no longer applies.
     - Ensure the active operator command in docs is the one-command runtime validator, not the old artifact-only wrapper.
   - Remediation status:
     - `design/vortex/PLAN.md` now states that Phase 3 owns live `VortexBasic` runtime capture validation.
     - `design/vortex/PLAN.md` now names `tools/vortex/Run-VortexBasicRuntimeValidation.ps1` as the supported Phase 3 runtime entrypoint.
     - The active `.planning` validation table already points the Phase 03 runtime rows at the one-command validator.

3. [x] **Legacy frame-10 closeout scripts are still presented as current Phase 3 validation**

   - Severity: `High`
   - Direction: `docs/tooling -> operator workflow`
   - Classification: `genuine drift that happens during project implementations`
   - Problem:
     - The validation table still advertises the old frame-10 closeout flow.
     - The old scripts still state that runtime validation is deferred to Phase 4.
   - Doc refs:
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md:47`
   - Code / tooling refs:
     - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1:6`
     - `tools/vortex/AnalyzeDeferredCoreCapture.py:3`
     - `tools/vortex/Verify-DeferredCoreCloseout.ps1:34`
     - `tools/vortex/Assert-DeferredCoreCaptureReport.ps1:160`
   - Exact remediation:
     - Remove the old closeout scripts from the active Phase 3 validation table.
     - If they must remain, move them into a `legacy` subsection of `tools/vortex/README.md`.
     - Add “do not use for current Phase 3 closure” warnings to those scripts and their comments.
     - Update `03-VALIDATION.md` so the current Phase 3 operator path is only the one-command `VortexBasic` runtime validation flow.
   - Remediation status:
     - `03-VALIDATION.md` now marks the old `03-15-01` closeout pack as historical / legacy instead of active Phase 3 validation.
     - `Run-DeferredCoreFrame10Capture.ps1` and `Assert-DeferredCoreCaptureReport.ps1` now explicitly describe themselves as historical 03-15 frame-10 tooling.
     - `tools/vortex/README.md` now contains a dedicated legacy subsection for the old frame-10 closeout pack.

4. [x] **Top-level requirements still disagree with the implemented Phase 3 local-light contract**

   - Severity: `High`
   - Direction: `both`
   - Classification: `USER did a bad job expressing requirements and design`
   - Problem:
     - PRD / PLAN still describe fullscreen/stencil-bounded local-light deferred behavior.
     - The authoritative LLD and code implement one-pass bounded-volume local-light rendering.
   - Doc refs:
     - `design/vortex/PRD.md:300`
     - `design/vortex/PRD.md:375`
     - `design/vortex/PLAN.md:277`
     - `design/vortex/PLAN.md:329`
     - `design/vortex/PLAN.md:353`
     - `design/vortex/lld/deferred-lighting.md:111`
     - `design/vortex/lld/deferred-lighting.md:121`
   - Code refs:
     - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:504`
     - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:1473`
     - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h:326`
   - Exact remediation:
     - **Adopt the UE 5.7 deferred-local-light contract as the Phase 3 authority.**
     - **UE 5.7 source basis**
       - `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\LightRendering.cpp`
         - `RenderLights(...)` orders and dispatches deferred direct lighting.
         - `RenderLight(...)` / `InternalRenderLight(...)` use one standard deferred-lighting path per local light; no separate stencil-mark pass is part of the normal local-light algorithm.
         - `InternalSetBoundingGeometryRasterizerState(...)` and `SetBoundingGeometryRasterizerAndDepthState(...)` define the outside-volume vs inside-volume raster/depth split.
       - `F:\Epic Games\UE_5.7\Engine\Shaders\Private\DeferredLightVertexShaders.usf`
         - point / spot lights use bounding proxy geometry in the vertex stage.
       - `F:\Epic Games\UE_5.7\Engine\Shaders\Private\DeferredLightPixelShaders.usf`
         - point / spot lighting reconstructs from scene depth, reads deferred inputs, and adds lighting directly into scene color.
       - `F:\Epic Games\UE_5.7\Engine\Shaders\Private\DeferredLightingCommon.ush`
         - attenuation and spot-cone terms are evaluated in shader from reconstructed world position and light constants.
     - **Final authoritative Stage 12 contract to document**
       - Directional lights:
         - one fullscreen triangle draw per directional light
         - additive blend into `SceneColor`
         - no depth test requirement beyond the existing fullscreen deferred path
       - Point / spot lights:
         - one bounded-volume lighting draw per light
         - no separate stencil-mark pass in the standard Phase 3 algorithm
         - inputs are `SceneDepth` + active GBuffers / deferred bindings
         - output is additive accumulation into `SceneColor` only
         - depth remains read-only; depth writes are disabled
         - stencil is not a required Stage 12 product
       - Outside-volume local-light mode:
         - render front faces
         - cull back faces
         - depth test enabled
         - depth compare uses the engine's reverse-Z aware compare (`GreaterOrEqual` for reverse-Z, `LessOrEqual` otherwise)
         - blend is additive on `SceneColor`
       - Inside-volume local-light mode:
         - render back faces
         - cull front faces
         - depth test enabled
         - depth compare is `Always`
         - depth writes disabled
         - blend is additive on `SceneColor`
       - Non-perspective local-light mode:
         - document explicitly whether it is a separate fallback label or shares the inside-volume state contract
         - if retained as a separate code path, it must still remain one bounded-volume lighting draw, not a stencil-mark sequence
     - **Exact documentation changes required**
       - `design/vortex/PRD.md`
         - replace the Stage 12 wording at lines around `299-300` and `374-375`
         - remove `fullscreen/stencil-bounded` wording for point / spot lights
         - restate the correctness-first Phase 3 path as:
           - directional = fullscreen additive deferred
           - point / spot = one-pass bounded-volume additive deferred
       - `design/vortex/PLAN.md`
         - rewrite the Stage 12 contract rows around `277`, `329`, and `353`
         - replace `point stencil-sphere` / `spot stencil-cone` with one-pass bounded-volume point / spot lighting
         - update exit-gate language so validation proves bounded-volume point / spot contribution, not stencil-local-light behavior
         - update the Phase matrix row around `876` so it no longer says `Pass-per-light fullscreen/stencil`
       - `design/vortex/ARCHITECTURE.md`
         - remove any remaining text that implies stencil-volume geometry is the canonical Phase 3 contract
         - restate Stage 12 ownership as temporary inline `SceneRenderer` orchestration over the bounded-volume deferred-light contract that later moves to `LightingService`
       - `design/vortex/lld/deferred-lighting.md`
         - keep this file as the authoritative Stage 12 contract
         - expand the existing bounded-volume section so it explicitly names:
           - directional fullscreen additive path
           - point / spot one-pass bounded-volume path
           - outside-volume state rules
           - inside-volume state rules
           - exact depth-write / stencil expectations
           - whether non-perspective views are an alias of inside-volume behavior or a separately named fallback
       - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
         - remove any active requirement that Stage 12 prove stencil-local-light behavior
         - state that the active runtime proof is:
           - one directional fullscreen draw
           - one point bounded-volume draw
           - one spot bounded-volume draw
           - nonzero point / spot `SceneColor` contribution when the lights cover visible geometry
       - `design/vortex/IMPLEMENTATION-STATUS.md`
         - remove any “unresolved local-light gap” wording once the above docs and the live validator agree on the bounded-volume contract
     - **Exact code changes required**
       - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
         - keep `BuildDeferredLocalPipelineDesc(...)` as the single owner of point / spot local-light pipeline setup
         - document and, if necessary, rename `DeferredLocalLightDrawMode` values so they match the final contract:
           - `kOutsideVolume`
           - `kCameraInsideVolume`
           - `kDirectFallback` only if a distinct non-perspective mode is intentionally retained
         - ensure the raster/depth setup at lines around `522-550` is described in code comments using the final contract terms above
         - keep the runtime dispatch at lines around `1471-1489` as one draw per point / spot light
       - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl`
         - remove `DeferredLightPointStencilMarkPS`
         - keep only the bounded-volume lighting path (`DeferredLightPointPS`) plus the volume vertex shader
       - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl`
         - remove `DeferredLightSpotStencilMarkPS`
         - keep only the bounded-volume lighting path (`DeferredLightSpotPS`) plus the volume vertex shader
       - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
         - remove `DeferredLightPointStencilMarkPS` and `DeferredLightSpotStencilMarkPS` from the Vortex shader catalog
         - ensure the catalog exposes only the live Stage 12 shader family:
           - directional VS/PS
           - point VS/PS
           - spot VS/PS
       - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli`
         - explicitly document that the helper library is for bounded-volume local-light evaluation and directional fullscreen evaluation
         - if proxy-geometry generation remains procedural, document that as an intentional deviation from UE's persistent mesh ownership model
       - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
         - align comments / state naming so Stage 12 no longer sounds stencil-driven
     - **Exact dead paths to remove or explicitly deprecate**
       - `DeferredLightPointStencilMarkPS`
       - `DeferredLightSpotStencilMarkPS`
       - any comments, names, or validation text that describe point / spot local lights as `stencil-bounded`
       - any active validation expectations that require stencil clear / stencil-mark products for Stage 12 success
       - if `kDirectFallback` remains only as a naming artifact for non-perspective bounded-volume lighting, rename it or document it explicitly to avoid implying a different algorithm
     - **Exact tests and validation updates required**
       - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
         - keep the current draw-shape assertions:
           - one point-light draw
           - one spot-light draw
           - directional fullscreen draw
         - keep the current mode assertions:
           - outside-volume uses `Vortex.DeferredLight.Point.Lighting` / `...Spot.Lighting`
           - inside-volume uses `...InsideVolumeLighting`
           - non-perspective mode uses the documented final name if retained
         - remove any future or stale expectation that Stage 12 success depends on stencil-mark passes
       - `tools/vortex/AnalyzeRenderDocVortexBasicCapture.py`
         - keep `EXPECTED_STAGE12_POINT_DRAW_COUNT = 1`
         - keep `EXPECTED_STAGE12_SPOT_DRAW_COUNT = 1`
         - keep `EXPECTED_STAGE12_POINT_STENCIL_CLEAR_COUNT = 0`
         - keep `EXPECTED_STAGE12_SPOT_STENCIL_CLEAR_COUNT = 0`
         - ensure comments and report labels make clear that zero stencil clears is the expected bounded-volume contract, not an accidental absence
       - `tools/vortex/AnalyzeRenderDocVortexBasicProducts.py`
         - keep point / spot nonzero `SceneColor` contribution as an active success signal
         - do not add stencil-mark product requirements
       - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1`
         - keep point / spot nonzero Stage 12 `SceneColor` assertions as part of the real gate
         - do not describe them as diagnostic-only if the script hard-fails on them
     - **Acceptance criteria for closing item 4**
       - PRD, PLAN, ARCHITECTURE, and `deferred-lighting.md` all describe the same Stage 12 contract
       - no active document still says point / spot local lights are `fullscreen/stencil-bounded`
       - `EngineShaderCatalog.h` no longer advertises `*StencilMarkPS` for the standard Vortex deferred-light path
       - point / spot shaders no longer define the dead stencil-mark entrypoints
       - `SceneRendererDeferredCore_test.cpp` proves one bounded-volume draw per point / spot light and the expected outside-volume / inside-volume / non-perspective state selection
       - the one-command runtime validator proves:
         - one point local-light draw
         - one spot local-light draw
         - zero required stencil clears for point / spot success
         - nonzero point / spot `SceneColor` contribution
     - **Ordered implementation steps**
       - 1. Rewrite `PRD.md`, `PLAN.md`, `ARCHITECTURE.md`, and `lld/deferred-lighting.md` so they all describe the same bounded-volume contract.
       - 2. Remove `DeferredLightPointStencilMarkPS` and `DeferredLightSpotStencilMarkPS` from shader code and `EngineShaderCatalog.h`.
       - 3. Audit `SceneRenderer.cpp` comments, draw-mode names, and debug pipeline names so they describe bounded-volume local-light lighting rather than stencil-era behavior.
       - 4. Update `SceneRendererDeferredCore_test.cpp` names/comments if needed so they prove the final contract rather than historical terminology.
       - 5. Update `AnalyzeRenderDocVortexBasicCapture.py`, `AnalyzeRenderDocVortexBasicProducts.py`, and `Assert-VortexBasicRuntimeProof.ps1` comments / report text so the runtime gate matches the final contract exactly.
       - 6. Update `03-VALIDATION.md` and `IMPLEMENTATION-STATUS.md` so the closure story uses the same contract.
       - 7. Rerun the one-command runtime validator and keep the resulting proof artifacts as the closure evidence for item 4.
     - **Explicit residual ambiguity to resolve during implementation**
       - Whether Vortex will keep procedural cone / sphere generation in `DeferredLightingCommon.hlsli` as an intentional deviation from UE's persistent proxy-geometry ownership model, or move to cached renderer-owned proxy meshes.
       - Whether `kDirectFallback` remains a separately named non-perspective path or is collapsed into the inside-volume contract with clearer naming.
   - Remediation status:
     - `design/vortex/PRD.md`, `design/vortex/PLAN.md`, `design/vortex/ARCHITECTURE.md`, `design/vortex/DESIGN.md`, `design/vortex/lld/README.md`, `design/vortex/lld/lighting-service.md`, and `design/vortex/lld/deferred-lighting.md` now all describe the same Stage 12 contract: directional fullscreen deferred lighting plus one-pass bounded-volume point/spot lighting.
     - The dead standard-path stencil-mark entrypoints were removed from `DeferredLightPoint.hlsl`, `DeferredLightSpot.hlsl`, and `EngineShaderCatalog.h`.
     - The retained non-perspective local-light mode was renamed from `DirectFallback` to `NonPerspective` in `SceneRenderer` state, debug names, and deferred-core tests.
     - The historical frame-10 analyzer/assertion pack was updated so it no longer points at removed stencil-mark entrypoints or stale stencil-bounded local-light names.
     - The deferred-lighting LLD now records the current intentional UE deviation explicitly: Phase 03 keeps the bounded-volume contract but still generates point/spot proxy geometry procedurally from `SV_VertexID` instead of using persistent cached proxy meshes.
     - Verification:
       - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
       - `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererDeferredCore.Tests.exe`
       - `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererPublication.Tests.exe`
       - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation`
       - Runtime proof remained green with `analysis_result=success`, `overall_verdict=pass`, one point draw, one spot draw, zero required Stage 12 stencil clears, and nonzero point/spot/directional `SceneColor`.

5. [x] **Point / spot local-light product contract is contradictory across docs, status, and asserts**

   - Severity: `High`
   - Direction: `code->docs`
   - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
   - Problem:
     - Some docs still describe point/spot product checks as diagnostic-only or unresolved.
     - The current runtime validator fails if point/spot `SceneColor` is zero.
   - Doc refs:
     - `design/vortex/IMPLEMENTATION-STATUS.md:100`
     - `design/vortex/IMPLEMENTATION-STATUS.md:1398`
     - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md:68`
     - `tools/vortex/README.md:35`
   - Code / tooling refs:
     - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1:72`
     - `tools/vortex/AnalyzeRenderDocVortexBasicProducts.py:303`
     - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation.validation.txt`
   - Exact remediation:
     - Choose one of two honest contracts:
       - make point/spot nonzero `SceneColor` an explicit Phase 3 requirement everywhere, or
       - relax the validator and mark the fields truly diagnostic
     - Update `README.md`, validation docs, and implementation-status text to match the enforced contract exactly.
   - Remediation status:
     - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1` now documents point/spot nonzero `SceneColor` as part of the durable runtime gate rather than as diagnostic-only.
     - `design/vortex/IMPLEMENTATION-STATUS.md` no longer describes the current point/spot local-light product gate as unresolved or merely diagnostic.
     - `tools/vortex/README.md` and `.planning/.../03-VALIDATION.md` were already aligned with the enforced gate and did not require further changes in this item.
     - Verification:
       - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic --parallel 4`
       - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/task21-05-pointspot-product-gate`
       - Runtime proof remained green with `analysis_result=success`, `overall_verdict=pass`, `stage12_point_scene_color_nonzero=true`, and `stage12_spot_scene_color_nonzero=true`.

6. [x] **Stage 9 dynamic velocity completion is missing**

   - Severity: `High`
   - Direction: `docs->code`
   - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
   - Problem:
     - Docs require Stage 3 static velocity plus Stage 9 completion for dynamic / skinned / WPO geometry.
     - BasePass never binds or writes a velocity render target.
   - Doc refs:
     - `design/vortex/ARCHITECTURE.md:759`
     - `design/vortex/PLAN.md:309`
     - `design/vortex/PLAN.md:323`
     - `design/vortex/lld/base-pass.md:227`
     - `design/vortex/lld/base-pass.md:391`
   - Code refs:
     - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:278`
     - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:395`
     - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl:106`
     - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:811`
   - Exact remediation:
     - Add a real Stage 9 velocity completion path for dynamic geometry.
     - Ensure the velocity output target is bound and written in the BasePass path.
     - Add tests that validate real velocity output content, not just a completion flag.
     - If this work is deferred, rewrite all docs/status/tests to describe the seam honestly as incomplete.
   - Remediation status:
     - Closed under the current-engine Phase 03 scope decision.
     - Stage 9 now binds and writes a real velocity target, publishes velocity
       via output-backed proof, and validates it through:
       - focused deferred-core tests
       - live D3D12 `VortexBasic` runtime proof
     - The remaining future work is no longer "dynamic velocity completion";
       it is deferred skinned / morph engine enablement tracked as explicit
       TODO carry-forward work.

7. [x] **Velocity is falsely reported complete**

   - Severity: `High`
   - Direction: `code->docs`
   - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
   - Problem:
     - The runtime and tests treat velocity as complete even though the completion pass does not exist.
   - Doc refs:
     - `design/vortex/ARCHITECTURE.md:578`
     - `design/vortex/ARCHITECTURE.md:579`
     - `design/vortex/PLAN.md:817`
   - Code refs:
     - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:395`
     - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:811`
     - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp:548`
   - Exact remediation:
     - Replace the boolean-only notion of completion with output-backed proof.
     - Make publication and test assertions depend on actual velocity writes.
     - Remove “completed” wording from status / docs until the output path is real.
   - Remediation status:
     - Closed under the current-engine Phase 03 scope decision.
     - The active closure surface now relies on output-backed velocity proof:
       - Stage 9 velocity MRT writes
       - MVWO auxiliary draw + merge dispatch
       - live D3D12 runtime validation artifacts
     - Status/docs/tracker now describe skinned / morph as deferred TODO
       work instead of presenting the current-engine closure as false full
       parity.

8. [x] **Masked materials are accepted into deferred base pass but never alpha-clip**

   - Severity: `High`
   - Direction: `docs->code`
   - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
   - Problem:
     - Masked draws are routed into Stage 9.
     - The runtime never selects an alpha-test shader permutation, so masked content behaves as opaque.
   - Doc refs:
     - `design/vortex/ARCHITECTURE.md:758`
     - `design/vortex/PLAN.md:317`
     - `design/vortex/PLAN.md:319`
     - `design/vortex/lld/base-pass.md:41`
     - `design/vortex/lld/base-pass.md:447`
   - Code refs:
     - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp:117`
     - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:202`
     - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Materials/MaterialTemplateAdapter.hlsli:25`
     - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h:292`
   - Exact remediation:
     - Introduce masked vs opaque Stage 9 PSO selection.
     - Drive the `ALPHA_TEST` permutation for masked materials.
     - Add a masked-material regression case that proves clip/discard behavior reaches the GBuffers correctly.
   - Remediation status:
     - Closed.
     - Masked vs opaque Stage 9 PSO selection now exists and drives the
       `ALPHA_TEST` permutation with regression coverage in the deferred-core
       test surface.

9. [x] **Phase 3 debug visualization is incomplete and not wired into execution**

   - Severity: `High`
   - Direction: `docs->code`
   - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
   - Problem:
     - Docs promise multiple GBuffer and depth debug views.
     - Code only contains partial shader scaffolding and no runtime route for those views.
   - Doc refs:
     - `design/vortex/PLAN.md:338`
     - `design/vortex/PLAN.md:344`
     - `design/vortex/ARCHITECTURE.md:1860`
   - Code refs:
     - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:765`
     - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp:120`
     - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl:17`
     - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h:297`
   - Exact remediation:
     - Define an actual runtime-facing debug-view selection contract.
     - Add the missing depth debug mode if it is still a Phase 3 requirement.
     - Route the selected debug visualization through `SceneRenderer`.
     - Add capture-backed validation for each promised debug view, or narrow the docs.
   - Remediation status:
     - Vortex now exposes a public `ShaderDebugMode` contract that is threaded
       through `Renderer` into `RenderContext`.
     - `SceneRenderer` now executes a real deferred debug-visualization pass
       after the Stage 10 rebuild boundary and before Stage 12 lighting, and
       it bypasses Stage 12 when a supported deferred debug mode is active.
     - `BasePassDebugView.hlsl` now supports the promised deferred modes:
       - base color
       - world normals
       - roughness
       - metalness
       - scene-depth-raw
       - scene-depth-linear
     - `BasePassDebugView.hlsl` now follows the standard `ViewConstants ->
       ViewFrameBindings` route instead of taking a raw root-constant slot.
     - `VortexBasic` now exposes `--shader-debug-mode`, and the live D3D12
       proof surface now validates the deferred debug modes through:
       - `tools/vortex/Run-VortexBasicDebugViewValidation.ps1`
       - per-mode RenderDoc reports under
         `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-debug-view-validation-*.validation.txt`

10. [x] **Stage 9 products are considered valid one stage later than the architecture says**

    - Severity: `High`
    - Direction: `docs->code`
    - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
    - Problem:
      - Architecture says active GBuffers plus `SceneColor` are valid after Stage 9.
      - Current code/test seam leaves them invalid until Stage 10.
    - Doc refs:
      - `design/vortex/ARCHITECTURE.md:942`
      - `design/vortex/ARCHITECTURE.md:963`
      - `design/vortex/PLAN.md:321`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:803`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:885`
      - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp:453`
    - Exact remediation:
      - Decide whether validity must become true at Stage 9 or remain Stage 10-owned.
      - Align code, tests, and architecture to the same boundary.
      - Do not leave “Stage 9 produces it but Stage 10 makes it valid” half-documented.
    - Remediation status:
      - Resolved in favor of **Stage 10-owned validity** for `SceneColor` and
        the active GBuffers.
      - Decision rationale:
        - UE5.7 does not expose a separate top-level “Stage 10” promotion seam;
          deferred consumers read the same scene-texture family directly after
          the base pass.
        - Vortex explicitly introduced Stage 10 as a
          `RebuildWithGBuffers()` + routing-refresh boundary, so in Vortex the
          only truthful meaning of “valid” is “consumable through the canonical
          `SceneTextureBindings` / `ViewFrameBindings` publication stack.”
        - Stage 9 therefore remains:
          - raw attachment production for `SceneColor` + active GBuffers
          - output-backed velocity production/publication
        - Stage 10 remains:
          - the first consumable publication boundary for `SceneColor` +
            active GBuffers + stencil-backed routing
      - Code/tests already matched this boundary; the remediation was to make
        the architecture and LLD tell the same story and to harden the runtime
        code comment/guard so Stage 9 cannot silently start publishing those
        bindings in the future.

11. [x] **SceneRenderer shell ownership drifted from `SceneRenderer` to `Renderer`**

    - Severity: `High`
    - Direction: `docs->code`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - LLD says `SceneRenderer` iterates per-view over `ctx.frame_views`.
      - Actual code selects one active view in `Renderer` and calls `SceneRenderer::OnRender` once.
    - Doc refs:
      - `design/vortex/lld/scene-renderer-shell.md:308`
    - Code refs:
      - `src/Oxygen/Vortex/Renderer.cpp:1001`
      - `src/Oxygen/Vortex/Renderer.cpp:1064`
      - `src/Oxygen/Vortex/Renderer.cpp:1301`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:765`
    - Exact remediation:
      - Either restore per-view ownership to `SceneRenderer`, or
      - rewrite shell/init-views/multi-view docs so `Renderer` is the authoritative selector / iterator.
    - Remediation status:
      - Resolved in favor of **Renderer-owned current-view selection / iteration**
        with `SceneRenderer` consuming the selected current view only.
      - Decision rationale:
        - UE5.7's scene renderer iterates its `Views` array internally because
          UE does not split an outer Renderer-Core layer from the scene-renderer
          family.
        - Oxygen does split those layers, and `Renderer` already owns:
          - canonical published runtime views
          - `RenderContext` materialization
          - per-view publication helpers
          - composition planning / target resolution
        - In Oxygen's layered architecture, keeping the outer current-view
          selection loop in `Renderer` is the cleaner and more future-proof
          ownership boundary; pushing that loop down into `SceneRenderer` starts
          coupling it to view-registration/publication/extraction responsibilities
          that belong to `Renderer Core`.
      - Code already matched this ownership boundary.
      - The remediation was to align the architecture and LLD package so they
        explicitly state:
        - `Renderer` materializes the eligible frame-view set
        - `Renderer` selects the current scene-view cursor
        - `SceneRenderer` owns the stage chain for that current view

12. [x] **`InitViewsModule` still uses the old ScenePrep shim instead of the explicit API the LLD requires**

    - Severity: `Medium`
    - Direction: `docs->code`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - LLD expects explicit `BeginFrameCollection / PrepareView / FinalizeView`.
      - Runtime still uses `Collect(..., reset_state)` and `Finalize()`.
    - Doc refs:
      - `design/vortex/lld/init-views.md:133`
      - `design/vortex/lld/sceneprep-refactor.md:285`
    - Code refs:
      - `src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h:50`
      - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp:36`
    - Exact remediation:
      - Promote the explicit API into `ScenePrepPipeline`.
      - Convert `InitViewsModule` to use it directly.
      - Remove the old boolean-reset shim once no callers depend on it.
    - Remediation status:
      - `ScenePrepPipeline` now exposes `BeginFrameCollection(...)`,
        `PrepareView(...)`, and `FinalizeView(...)` as the live Vortex runtime
        API.
      - `InitViewsModule` now calls the explicit pipeline API directly; the
        local adapter helpers that forwarded to `Collect(..., reset_state)` /
        `Finalize()` were removed.
      - The old boolean-reset shim was removed from the Vortex ScenePrep API.
        The remaining `CollectSingleView(...)` entrypoint is an explicit fused
        harness/test path rather than a runtime compatibility layer.
      - The explicit API now fails closed on misuse:
        `PrepareView(...)` requires the matching frame-phase context, and
        `FinalizeView(...)` requires the same `ScenePrepState` that prepared the
        active view.
      - `ScenePrepPipeline_collection_test.cpp` now exercises the
        phase-explicit flow directly, including `FinalizeView(...)`.

13. [ ] **`PreparedSceneFrame` exposes shadow payload fields that Stage 2 never publishes**

    - Severity: `Medium`
    - Direction: `code->docs`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - The published Phase 3 contract exposes shadow payload fields.
      - Stage 2 never fills them.
    - Doc refs:
      - `design/vortex/lld/sceneprep-refactor.md:162`
    - Code refs:
      - `src/Oxygen/Vortex/PreparedSceneFrame.h:63`
      - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp:83`
    - Exact remediation:
      - Either populate those fields in Phase 3 publication, or
      - remove/defer them from the Phase 3 contract and document them as later-phase surfaces.

14. [ ] **Stage 10 ownership is split between `SceneTextures` and `SceneRenderer`**

    - Severity: `Medium`
    - Direction: `both`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - Docs imply one owner for Stage 10 rebuild/publication.
      - Code splits it across `SceneTextures::RebuildWithGBuffers()` and `SceneRenderer` republishing.
    - Doc refs:
      - `design/vortex/lld/scene-textures.md:389`
      - `design/vortex/ARCHITECTURE.md:755`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp:218`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:887`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:993`
    - Exact remediation:
      - Choose a single Stage 10 owner, or
      - explicitly document the two-part contract and its responsibilities.

15. [ ] **`scene-textures.md` is stale on publication timing and API shape**

    - Severity: `Medium`
    - Direction: `docs->code`
    - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
    - Problem:
      - LLD still describes raw stencil/custom-stencil access and Stage 9 publication.
      - Code uses `SceneTextureAspectView` and Stage 10 republish.
    - Doc refs:
      - `design/vortex/lld/scene-textures.md:118`
      - `design/vortex/lld/scene-textures.md:389`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.h:176`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:1025`
      - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp:449`
    - Exact remediation:
      - Rewrite `scene-textures.md` around current accessor types and the actual Stage 10 boundary.

16. [ ] **Deferred-lighting LLD no longer matches the actual geometry/constants model**

    - Severity: `Medium`
    - Direction: `docs->code`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - LLD still describes cached sphere/cone geometry and `register(b1)` style light constants.
      - Runtime uses procedural volume vertices and bindless-selected constants.
    - Doc refs:
      - `design/vortex/lld/deferred-lighting.md:96`
      - `design/vortex/lld/deferred-lighting.md:126`
      - `design/vortex/lld/deferred-lighting.md:214`
    - Code refs:
      - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl:9`
      - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl:9`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:504`
    - Exact remediation:
      - Update the LLD to the current bindless/procedural contract, or
      - revert the implementation to the documented cached-geometry model.

17. [ ] **The one-command validator does not enforce the manual D3D12 debug-layer cleanliness gate**

    - Severity: `Medium`
    - Direction: `docs->runtime`
    - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
    - Problem:
      - `03-20` requires debugger-assisted proof with no D3D12 errors / breaks.
      - `Run-VortexBasicRuntimeValidation.ps1` currently enforces build + capture + analyzer success only.
    - Doc refs:
      - `.planning/workstreams/vortex/phases/03-deferred-core/03-20-PLAN.md:20`
      - `.planning/workstreams/vortex/phases/03-deferred-core/03-20-PLAN.md:51`
    - Code refs:
      - `tools/vortex/Run-VortexBasicRuntimeValidation.ps1`
      - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1:96`
    - Exact remediation:
      - Either add explicit stderr / debug-layer failure gating to the one-command validator, or
      - formally retire the old debugger-cleanliness requirement in the docs.

18. [ ] **PRD commits a broader logical SceneTextures family than the live ABI actually reserves**

    - Severity: `Medium`
    - Direction: `code->docs`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - PRD commits logical slots beyond the current four active GBuffers.
      - The live ABI does not reserve the extra stable slots.
    - Doc refs:
      - `design/vortex/PRD.md:72`
      - `design/vortex/PRD.md:257`
      - `design/vortex/PRD.md:335`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.h:155`
      - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli:13`
    - Exact remediation:
      - Either reserve the future-facing slots now in the ABI, or
      - narrow the PRD to the currently implemented ABI.

19. [ ] **Phase 3 status matrix in `PLAN.md` is stale**

    - Severity: `Medium`
    - Direction: `code->docs`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - The status matrix still marks major Phase 3 surfaces as `not started`.
      - Code now has substantial implementation and tests for those surfaces.
    - Doc refs:
      - `design/vortex/PLAN.md:872`
      - `design/vortex/PLAN.md:876`
      - `design/vortex/PLAN.md:883`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp:188`
      - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp:312`
      - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:360`
      - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:1193`
    - Exact remediation:
      - Update the matrix to reflect current implementation reality.
      - Leave only genuine remaining gaps as open items.

20. [ ] **Stage 21 / 23 resolve-extract timing is inconsistently documented**

    - Severity: `Medium`
    - Direction: `both`
    - Classification: `USER did a bad job expressing requirements and design`
    - Problem:
      - Some docs still describe Stage 21 / 23 as later-phase or stub behavior.
      - Code performs real copy / extract work.
    - Doc refs:
      - `design/vortex/ARCHITECTURE.md:1187`
      - `design/vortex/PLAN.md:263`
      - `design/vortex/PLAN.md:883`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp:55`
      - `src/Oxygen/Vortex/SceneRenderer/PostRenderCleanup.cpp:55`
    - Exact remediation:
      - Collapse the docs to one explicit lifecycle story for resolve / extract.
      - State clearly whether these are already part of the Phase 3 retained branch or not.

21. [ ] **Depth prepass front-to-back refinement is documented but not implemented**

    - Severity: `Low`
    - Direction: `docs->code`
    - Classification: `genuine drift that happens during project implementations`
    - Problem:
      - The LLD promises front-to-back refinement.
      - The processor currently filters / accepts draws but does not sort them front-to-back.
    - Doc refs:
      - `design/vortex/lld/depth-prepass.md:188`
      - `design/vortex/lld/depth-prepass.md:317`
    - Code refs:
      - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.cpp:43`
    - Exact remediation:
      - Implement the documented ordering, or
      - delete the claim from the LLD.

22. [ ] **`BasePassDebugView.hlsl` bypasses the documented `ViewConstants -> ViewFrameBindings` route**

    - Severity: `Low`
    - Direction: `code->docs`
    - Classification: `AI fuckup because it is an arrogant piece of shit that disrespects instructions`
    - Problem:
      - The shader takes a raw root constant instead of following the standard `ViewConstants` flow.
    - Doc refs:
      - `design/vortex/ARCHITECTURE.md:993`
      - `design/vortex/ARCHITECTURE.md:1055`
    - Code refs:
      - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl:11`
    - Exact remediation:
      - Route the debug shader through the same `ViewConstants` / `ViewFrameBindings` path as the rest of the Vortex stack.

23. [ ] **The design package disagrees on where `DeferredShadingCommon.hlsli` should live**

    - Severity: `Low`
    - Direction: `docs->code`
    - Classification: `USER did a bad job expressing requirements and design`
    - Problem:
      - `PLAN.md` wants it in `Shared/`.
      - `ARCHITECTURE.md` says family-local common code should stay with the owning service unless reuse is proven.
      - Code follows the plan.
    - Doc refs:
      - `design/vortex/PLAN.md:288`
      - `design/vortex/ARCHITECTURE.md:1522`
      - `design/vortex/ARCHITECTURE.md:1672`
    - Code refs:
      - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/DeferredShadingCommon.hlsli:1`
    - Exact remediation:
      - Make the docs agree on the desired ownership rule, then either keep or move the file accordingly.

## Confirmed Aligned Areas

- [ ] **Stage spine exists and is real**
  - Stage 2 `InitViews`, Stage 3 `DepthPrepass`, Stage 9 `BasePass`, Stage 10 `RebuildWithGBuffers`, Stage 12 deferred lighting, Stage 21 resolve, and Stage 23 cleanup all exist in code.
- [ ] **Per-view publication stack is real**
  - `InitViews`, `SceneTextureBindings`, `ViewFrameBindings`, and renderer refresh / republish are present.
- [ ] **SceneTextures active subset exists**
  - `SceneColor`, `SceneDepth`, `PartialDepth`, four active GBuffers, velocity, custom depth, and stencil access are present in the runtime shape.
- [ ] **Shader family placement is broadly aligned**
  - Vortex stage / service shader families and catalog registration exist where the design expects them.
- [ ] **Deferred lighting is genuinely deferred**
  - Stage 12 consumes published GBuffer / depth bindings and accumulates into `SceneColor`; it is not a legacy forward path.
- [ ] **One-command runtime validation surface exists and is coherent**
  - `tools/vortex/Run-VortexBasicRuntimeValidation.ps1` now performs build + capture + analysis end-to-end.

## Notes

- This tracker is based on read-only review findings and existing proof artifacts.
- It does **not** itself prove runtime correctness; it tracks the closure work required to make Phase 3 docs, code, validation, and status surfaces tell the same truth.
