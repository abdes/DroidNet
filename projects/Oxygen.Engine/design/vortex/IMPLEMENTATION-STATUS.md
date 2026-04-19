# Vortex Renderer Implementation Status

Status: `gaps_found — Phase 4 code exists on the current branch, but direct UAT and phase verification still reject the remaining visual/parity closeout claim`

This document is the **running resumability ledger** for the Vortex renderer.
It records what is actually in the repo, what has been verified, what is still
missing, and exactly where to resume. All claims must be evidence-backed.

Related:

- [PRD.md](./PRD.md) — stable product requirements
- [ARCHITECTURE.md](./ARCHITECTURE.md) — stable architecture
- [DESIGN.md](./DESIGN.md) — stable design entry point and cross-subsystem rationale
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) — authoritative file layout
- [PLAN.md](./PLAN.md) — active execution plan

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## Ledger Rules

1. **Evidence, not intention.** Every entry records what exists in code and
   what validation was run, not what was planned or discussed.
2. **No-false-completion.** A phase is `done` only when: code exists,
   required docs are updated, and validation evidence is recorded here.
3. **Missing-delta explicit.** If build or tests were not run, the phase
   stays `in_progress` with the missing validation delta listed.
4. **Scope-drift trigger.** If scope changes or the current design is found
   incomplete, update design docs before claiming further progress.
5. **Per-session update.** Each implementation session must update this file
   with: changed files, commands run, results, and remaining blockers.

### 2026-04-19 — Stage 5 HZB core implementation summary

- Scope:
  - revalidated the Stage 5 HZB findings against current Vortex code and UE5.7
    source/shader references
  - corrected the Stage 5 HZB design before implementation
  - implemented the generic Stage 5 HZB core only
  - explicitly excluded consumer lanes, runtime validation flows, and proof
    tooling from this summary
- HZB core delivered:
  - explicit HZB request + valid-depth eligibility
  - UE5.7-shaped current-frame HZB mapping/publication surface
  - previous-frame furthest-HZB handoff plus previous-view rect publication
  - bindless routing through `ViewFrameBindings::screen_hzb_frame_slot`
  - focused Stage 5 HZB coverage in `SceneRendererPublication` and
    `SceneRendererDeferredCore`
- HZB-core files:
  - `design/vortex/lld/hzb.md`
  - `design/vortex/lld/occlusion.md`
  - `src/Oxygen/Vortex/RenderContext.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.cpp`
  - `src/Oxygen/Vortex/Types/ScreenHzbFrameBindings.h`
  - `src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `src/Oxygen/Vortex/Types/ViewHistoryFrameBindings.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ScreenHzbBindings.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/ViewHistoryFrameBindings.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/Hzb/ScreenHzbBuild.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererPublication Oxygen.Vortex.SceneRendererDeferredCore --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R "^(Oxygen\\.Vortex\\.(SceneRendererPublication|SceneRendererDeferredCore)\\.Tests|SceneRendererPublicationTest\\.|SceneRendererDeferredCore(Test|SurfaceTest)\\.)"`
- Result:
  - the Stage 5 HZB core is implemented in code
  - focused HZB core tests are green
  - the generic HZB publication/design surface is separated from
    consumer-specific policy in the committed core lane
- Remaining blocker:
  - capture-backed/runtime parity proof is still outstanding
  - consumer lanes are tracked separately and are not part of this HZB core
    summary

### 2026-04-19 — VortexBasic debugger audit is clean again, but the full live parity pack still has capture-product gaps

- Scope:
  - reran the live `VortexBasic` runtime surface under `cdb.exe`
  - reran a normal no-capture `VortexBasic` launch and inspected the emitted logs
  - repaired the Stage 5 Screen HZB depth-SRV path that was tripping the D3D12
    debug layer on `Depth32Stencil8`
  - updated the VortexBasic RenderDoc proof scripts to follow the real Stage 15
    local-fog indirect-draw command shape
- Changed files this session:
  - `src/Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.cpp`
  - `tools/vortex/AnalyzeRenderDocVortexBasicCapture.py`
  - `tools/vortex/AnalyzeRenderDocVortexBasicProducts.py`
  - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1`
  - `design/vortex/lld/occlusion.md`
  - `design/vortex/lld/environment-service.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `ctest --output-on-failure -R "SceneRendererPublicationTest.Stage5PublishesRealScreenHzbProductsForDeferredViews"`
  - `ctest --output-on-failure -R "EnvironmentLightingServiceSurfaceTest.Stage15ProofToolsUseFinalStage12BaselineAndEmitBlockingAsyncQualityKeys"`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/live/vortexbasic/2026-04-19-hzb-audit -BuildJobs 8`
  - `out/build-ninja/bin/Debug/Oxygen.Examples.VortexBasic.exe --frames 6 --fps 30 --capture-provider off`
- Result:
  - debugger-backed audit now passes again:
    `runtime_exit_code=0`, `d3d12_error_count=0`, `dxgi_error_count=0`,
    `blocking_warning_count=0`
  - the only accepted debugger warning is the documented shutdown
    `DXGI WARNING: Live IDXGIFactory ...` line
  - the normal no-capture runtime run exits `0` and its log scan found no real
    runtime error signatures (`D3D12 ERROR`, `DXGI ERROR`, break-exception, or
    loguru `ERROR`)
  - the full capture-backed VortexBasic validator remains **incomplete**:
    the generated products report still records `stage12_spot_scene_color_nonzero=false`,
    `stage12_point_scene_color_nonzero=false`,
    `stage12_directional_scene_color_nonzero=false`,
    `stage15_sky_scene_color_changed=false`,
    `stage15_local_fog_scene_color_changed=false`,
    and the current Screen-HZB capture-product probe still does not resolve the
    published HZB resource name from the capture
- Remaining blocker:
  - do not claim VortexBasic parity complete yet; debugger/runtime cleanliness
    is restored, but the live capture-backed product proof is still red and
    needs a dedicated follow-up pass

## Phase Summary

### 2026-04-19 — local fog parity claim corrected before rewrite

- The current Vortex local-fog implementation is now explicitly treated as
  **incomplete** against the UE5.7 local fog volume contract.
- Previous planning language that framed local fog as an already-landed
  first-wave feature that only needed hardening is superseded by the direct
  parity requirement.
- Required rewrite targets:
  - UE-style authored defaults and base-volume scaling
  - UE-style per-view translated packing, sort semantics, and capped instance
    selection
  - UE-style tiled culling resources and analytical splat path
  - explicit parity evidence against the relevant UE5.7 source/shader files
- Remaining parity blockers after the rewritten Stage 14/Stage 15 path:
  - live runtime proof still reports `screen_hzb_published=false`, which keeps
    `local_fog_hzb_consumed=false` in the capture-backed validator
  - height-fog inline local-fog composition parity is not yet implemented
  - volumetric-fog injection parity is blocked because Vortex does not yet have
    the volumetric fog system that UE5.7 local fog integrates with
  - sky-light scattering still uses the current Vortex environment products
    rather than the exact UE5.7 view-side SH/volumetric-scattering contract
- Until the rewrite lands and the required verification passes, do not report
  local fog as shipped, hardened, or proof-backed.

### 2026-04-19 — 04-18 closed with authorized local-fog interaction gaps

- `04-18` is now closed for the hard v3 content cut plus the
  scene/data/DemoShell local-fog vertical slice.
- Verification completed for the non-renderer ownership surface:
  - `Oxygen.Cooker.AsyncImportSceneDescriptor.Tests`
  - `Oxygen.Data.All.Tests`
  - `Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests`
  - `Oxygen.Examples.DemoShell.SceneLoaderServicePhase4.Tests`
  - `pytest src/Oxygen/Cooker/Tools/PakGen/tests/test_writer_scene_basic.py -q`
- DemoShell height-fog authoring was tightened so the panel wording and control
  ranges now match the Fog metadata contract it applies to.
- User-authorized deferred gaps remain open and are carried forward:
  - local fog / height fog inline composition parity
  - local fog / volumetric fog injection parity
  - capture-backed proof that still reports `screen_hzb_published=false` and
    `local_fog_hzb_consumed=false`
- This closes `04-18`, but it does **not** close local-fog UE5.7 parity as a
  whole. That gate remains open for `04-21`, `04-22`, `04-23`, `04-25`, and
  `04-29`.

### 2026-04-19 — 04-26 closed with widened non-localfog environment contracts

- `04-26` is now closed for the contract-widening lane.
- Landed scope:
  - widened non-localfog authored fields in `SkyAtmosphere`, `Fog`,
    `SkyLight`, and `DirectionalLight`
  - widened Vortex environment contract types plus CPU/HLSL mirror fields
  - DemoShell dirty-domain split for atmosphere model, atmosphere lights,
    height fog, volumetric fog, sky light, and presets
  - DemoShell UI/service/VM support for the widened atmosphere, sky-light,
    fog, and atmosphere-light-slot authoring surface
  - widened persistence schema and load/save coverage for sun
    atmosphere-light metadata
- New verification evidence:
  - `cmake --build --preset windows-debug --target oxygen-examples-demoshell Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R "EnvironmentSettingsService"`
  - `out\build-ninja\bin\Debug\Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests.exe --gtest_filter=EnvironmentSettingsServiceTest.AppliesAtmosphereLightMetadataToAuthoredSceneSunLight:EnvironmentSettingsServiceTest.AppliesAtmosphereLightMetadataToSyntheticSunLight:EnvironmentSettingsServiceTest.LoadSettings_ReadsSunAtmosphereLightMetadataKeys`
  - `cmake --build --preset windows-debug --target oxygen-scene Oxygen.Scene.EnvironmentComponents.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R "EnvironmentComponents"`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.EnvironmentLightingService.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R "EnvironmentLightingService"`
- Remaining truth:
  - `04-26` closes the widened contract lane only
  - it does **not** close the later runtime/parity/proof lanes:
    `04-19`, `04-27`, `04-20`, `04-28`, `04-21`, `04-22`, `04-23`, `04-29`,
    `04-24`, `04-30`, `04-25`, and `04-15`

| Phase | Name | Status | Blocker |
| ----- | ---- | ------ | ------- |
| 0 | Scaffold and Build Integration | `done` | — |
| 1 | Substrate Migration | `done` | — |
| 2 | SceneTextures + SceneRenderer Shell | `done` | — |
| 3 | Deferred Core | `done` | — |
| 4 | Migration-Critical Services + First Migration | `gaps_found` | Direct UAT and `04-VERIFICATION.md` reject the Phase 4 closeout claim |
| 5 | Remaining Services + Runtime Scenarios | `not_started` | Phase 4 + per-service/scenario LLDs |
| 6 | Legacy Deprecation | `not_started` | Phase 5 |
| 7 | Future Capabilities (post-release) | `not_started` | Phase 6 |

## Design Deliverable Tracker

Each design deliverable required by PLAN.md is tracked here. A phase's
implementation cannot begin until its design prerequisites are met.

| ID | Deliverable | Required By | Status | Location |
| -- | ----------- | ----------- | ------ | -------- |
| D.1 | SceneTextures four-part contract | Phase 2 | `done` | [`lld/scene-textures.md`](lld/scene-textures.md) |
| D.2 | SceneRenderBuilder bootstrap | Phase 2 | `done` | [`lld/scene-renderer-shell.md`](lld/scene-renderer-shell.md) |
| D.3 | SceneRenderer shell dispatch | Phase 2 | `done` | [`lld/scene-renderer-shell.md`](lld/scene-renderer-shell.md) |
| D.4 | Depth prepass LLD | Phase 3 | `done` | [`lld/depth-prepass.md`](lld/depth-prepass.md) |
| D.5 | Base pass LLD | Phase 3 | `done` | [`lld/base-pass.md`](lld/base-pass.md) |
| D.6 | Deferred lighting LLD | Phase 3 | `done` | [`lld/deferred-lighting.md`](lld/deferred-lighting.md) |
| D.7 | Shader contracts LLD | Phase 3 | `done` | [`lld/shader-contracts.md`](lld/shader-contracts.md) |
| D.8 | InitViews LLD | Phase 3 | `done` | [`lld/init-views.md`](lld/init-views.md) |
| D.9 | LightingService LLD | Phase 4A | `done` | [`lld/lighting-service.md`](lld/lighting-service.md) |
| D.10 | PostProcessService LLD | Phase 4B | `done` | [`lld/post-process-service.md`](lld/post-process-service.md) |
| D.11 | ShadowService LLD | Phase 4C | `done` | [`lld/shadow-service.md`](lld/shadow-service.md) |
| D.12 | EnvironmentLightingService LLD | Phase 4D | `done` | [`lld/environment-service.md`](lld/environment-service.md) |
| D.13 | Migration playbook | Phase 4E | `done` | [`lld/migration-playbook.md`](lld/migration-playbook.md) |
| D.14 | DiagnosticsService LLD | Phase 5A | `done` | [`lld/diagnostics-service.md`](lld/diagnostics-service.md) |
| D.15 | TranslucencyModule LLD | Phase 5B | `done` | [`lld/translucency.md`](lld/translucency.md) |
| D.16 | OcclusionModule LLD | Phase 5C | `done` | [`lld/occlusion.md`](lld/occlusion.md) |
| D.17 | Multi-view composition LLD | Phase 5D | `done` | [`lld/multi-view-composition.md`](lld/multi-view-composition.md) |
| D.18 | Offscreen rendering LLD | Phase 5E | `done` | [`lld/offscreen-rendering.md`](lld/offscreen-rendering.md) |

---

## Documentation Sync Log

### 2026-04-19 — direct user UAT closes the old Async overlay blocker, but Phase 4 still has open visual/parity gaps

- **Scope**:
  - reconciled the live tracking docs with the latest direct user UAT
  - closed the stale "Async has no visible ImGui overlay" blocker in the
    current Phase 4 failure-state documents
  - kept the remaining active gaps explicit instead of overstating progress
- **Changed files**:
  - `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/04-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/.continue-here.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- **Evidence used**:
  - direct user UAT on 2026-04-19: "Async has ImGui now and there is no such problem anymore."
  - existing retained harness/composition validation rows already recorded in `04-VERIFICATION.md`
- **Result**:
  - the old overlay/composition complaint is no longer treated as an active
    Phase 4 blocker
  - Phase 4 still remains `gaps_found`
- **Remaining blockers**:
  - poor sky quality
  - over-exposed output
  - tonemapping not yet visually confirmed
  - non-credible PBR material output
  - UE5.7 parity proof still not closed

### 2026-04-18 — 04-17 spotlight cone/runtime-light repair completed

- **Plan**: `04-17` — Repair direct-light/runtime-scene response on Async.
- **Changed files**:
  - `src/Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.cpp`
  - `src/Oxygen/Scene/Light/SpotLight.h`
- **What changed**:
  - deferred spotlight proxy cones now rotate to the authored spotlight
    direction instead of remaining aligned to the default axis
  - deferred spotlight proxy cone radius now uses `range * tan(theta)` at the
    outer cone, fixing the user-observed mismatch in outer-cone behavior
  - `scene::SpotLight` now canonicalizes authored inner/outer cone ordering at
    the component boundary
- **Commands used for verification**:
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LightingService.Tests --parallel 4`
  - `out/build-ninja/bin/Debug/Oxygen.Vortex.LightingService.Tests.exe`
- **Result**:
  - build passed
  - `Oxygen.Vortex.LightingService.Tests` passed (`5/5`)
  - direct manual runtime validation from the user confirmed:
    - the spotlight now casts visible light on the scene
    - the spotlight reflection is visible on the metallic spheres
- **Not run**:
  - full Async validator rerun for this task
  - broader `ctest -R "LightingService"` suite is not clean on this branch
    because unrelated `SceneRendererDeferredCoreTest` cases crash with
    heap-corruption (`0xc0000374`)
- **Phase 4 status**:
  - `04-17` is complete for the spotlight-runtime fix lane
  - Phase 4 overall remains `gaps_found` due to the wider environment parity
    program and other open lanes

### 2026-04-18 — 04-15: Async parity proof replaced with reference-based surface

- **Plan**: `04-15` — Replace hollow Async parity proof with a real
  reference baseline and route back to Phase 4 re-verification.
- **Dependency**: `04-17` (direct-light/PBR repair) — not yet executed;
  `04-15` lands the tooling infrastructure ahead of runtime fixes.
- Changed files:
  - `tools/vortex/Capture-AsyncLegacyReference.ps1` (new) — dedicated
    UE5.7 source/shader parity baseline capture for Async frame-10 proof
  - `tools/vortex/Run-AsyncRuntimeValidation.ps1` — added `-ReferenceRoot`
    parameter; current Vortex validation now consumes an external reference
    instead of self-initializing baseline artifacts
  - `tools/vortex/Verify-AsyncRuntimeProof.ps1` — added `-ReferenceRoot`
    parameter; baseline frame/depth loaded from external reference directory
  - `tools/vortex/AnalyzeRenderDocAsyncProducts.py` — promoted
    `stage12_directional_scene_color_nonzero` and
    `stage12_spot_scene_color_nonzero` into the `overall_verdict` gate
  - `tools/vortex/Assert-AsyncRuntimeProof.ps1` — added
    `stage12_directional_scene_color_nonzero` and
    `stage12_spot_scene_color_nonzero` to the required product checks
  - `tools/vortex/README.md` — documented the Async reference-based proof flow
  - `Examples/Async/README.md` — documented split `reference/` vs `current/`
    artifact layout
- Artifact layout (new):
  - `build/artifacts/vortex/phase-4/async/reference/` — legacy/reference
    baseline: `reference_frame10.png`, `reference_depth.png`,
    `reference_renderdoc.rdc`, `reference_behaviors.md`
  - `build/artifacts/vortex/phase-4/async/current/` — current Vortex capture:
    `current_renderdoc.rdc` plus validation reports
- Harness rerun evidence:
  - `Oxygen.Vortex.SinglePassHarnessFacade.Tests` — passed
  - `Oxygen.Vortex.RenderGraphHarnessFacade.Tests` — passed
  - `Oxygen.Vortex.RendererFacadePresets.Tests` — passed
  - Legacy `Oxygen.Renderer.*` harness targets have pre-existing failures
    (ViewConstants buffer creation in fake graphics layer); not in scope.
- Blocking assertion surface now includes:
  - overlay composition (`imgui_overlay_composited_on_scene`)
  - Stage 15 sky quality (`stage15_sky_quality_ok`)
  - Stage 22 exposure/tonemap (`stage22_exposure_clipping_ratio_ok`,
    `stage22_tonemap_output_nonzero`)
  - direct-light nonzero (`stage12_directional_scene_color_nonzero`,
    `stage12_spot_scene_color_nonzero`)
  - PBR-response (gated indirectly via direct-light and scene-color delta keys)
  - final presentation (`final_present_nonzero`,
    `final_present_vs_tonemap_changed`)
- Runtime validation NOT executed:
  - `Capture-AsyncLegacyReference.ps1` and `Run-AsyncRuntimeValidation.ps1
    -ReferenceRoot` require the Async example to run cleanly (currently
    exits with code 1 — dependent on `04-17` runtime fixes).
  - Full proof pack execution is deferred until `04-17` lands.
- Phase 4 status: remains `gaps_found`
- **Next step**: Phase 4 re-verification after `04-17` lands and the full
  reference-based proof pack executes green. Do not advance to Phase 5.

### 2026-04-18 — Phase 4 completion claim revoked after direct UAT and verifier failure

- Direct user UAT on 2026-04-18 reported the live Async demo is still not
  acceptable:
  - ImGui overlay not visibly composed on the scene
  - Async spheres not showing credible PBR material rendering
  - sky quality unacceptable
  - exposure over-exposed
  - tonemapping not visually confirmed
  - DemoShell migration still perceived as incomplete / legacy-biased
- The authoritative verifier now records Phase 4 as `gaps_found` in
  `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/04-VERIFICATION.md`.
- The advisory review in
  `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/04-REVIEW.md`
  also flags weak Async Stage 22 proof, weak Stage 15 proof derivation, and
  malformed debugger-audit evidence.
- Result:
  - earlier “Phase 4 is complete” wording is superseded
  - Phase 4 must not advance to Phase 5 yet
  - resume with a dedicated Phase 4 gap-planning pass grounded in direct UAT,
    `04-VERIFICATION.md`, and `04-REVIEW.md`
- Remaining blocker:
  - close the unresolved visual/parity/composition gaps before restoring any
    Phase 4 completion claim

### 2026-04-18 — Phase 4 composition/presentation closeout rerun clean on the debugger-backed Async path

- Scope decision:
  - `04-09` remains the owner of Async parity thresholds and harness proof.
  - `04-06` reuses the retained Async proof pack, strengthens the retained
    Stage 21 / 22 / 23 boundary documentation, and now also proves the live
    migrated runtime under `cdb`.
  - The runtime fix stayed inside the retained Phase 4 path:
    no new composition seam, no new renderer path, and no legacy fallback.
- Changed files this session:
  - `src/Oxygen/Graphics/Common/Internal/FramebufferImpl.h`
  - `src/Oxygen/Graphics/Common/Internal/FramebufferImpl.cpp`
  - `src/Oxygen/Vortex/Internal/ViewportClamp.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.cpp`
  - `src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h`
  - `src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.cpp`
  - `src/Oxygen/Vortex/Lighting/Passes/DeferredLightPass.cpp`
  - `src/Oxygen/Vortex/Environment/Passes/SkyPass.cpp`
  - `src/Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.cpp`
  - `src/Oxygen/Vortex/Environment/Passes/FogPass.cpp`
  - `src/Oxygen/Vortex/PostProcess/Passes/TonemapPass.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/PostRenderCleanup.cpp`
  - `Examples/Async/README.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-vortexbasic Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererShell.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R "^(Oxygen\.Vortex\.(RendererCompositionQueue|CompositionPlanner|SceneRendererShell)\.Tests|RendererCompositionQueueTest\.|CompositionPlannerTest\.|SceneRendererShellProofSurfaceTest\.)"`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-06`
  - `powershell -NoProfile -File tools/vortex/Verify-AsyncRuntimeProof.ps1 -CapturePath build/artifacts/vortex/phase-4/async/baseline_renderdoc.rdc`
  - `cdb.exe -G -g -logo build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.log -cf build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.commands.txt out/build-ninja/bin/Debug/Oxygen.Examples.Async.exe --frames 14 --fps 30 --vsync false --debug-layer true --capture-provider off`
- Result:
  - composition queue / planner / shell regressions pass on the retained
    migrated single-view path
  - `VortexBasic` remains green for the systems-and-passes validator lane
  - `Verify-AsyncRuntimeProof.ps1` still passes against the retained Async
    proof pack
  - the debugger-backed Async rerun now reaches `exit code: 0`
  - the D3D12 debug-layer failures that blocked the earlier closeout are gone:
    `#821`, `#1378`, `#538`, and the previously removed `#615`
  - the only remaining debugger-surface noise is the accepted shutdown
    `DXGI WARNING: Live IDXGIFactory ...` line
- Code / validation delta:
  - CPU-only framebuffer RTV/DSV views are now per-framebuffer allocations
    instead of shared registry-cached views, which prevents descriptor-slot
    aliasing between the resized scene-depth path and the retained shadow path
  - temporary Stage 15 framebuffers are now deferred-released so their CPU
    descriptor slots stay valid until GPU completion
  - the shadow surface uses the stencil-capable depth format the runtime path
    actually binds
  - viewport/scissor setup on the retained runtime passes is clamped to their
    actual target extents
- Remaining blocker:
  - none for `04-06`

### 2026-04-18 — Phase 4 composition/presentation closeout blocked by debugger-backed Async audit

- Scope decision:
  - `04-09` remains the owner of Async parity thresholds and harness proof.
  - `04-06` still reuses the existing Async artifact pack to inspect only the
    composition/presentation boundary on the migrated path.
  - `ResolveSceneColor` remains the Stage 21 owner when resolve is required.
  - `PostRenderCleanup` remains the Stage 23 extraction/handoff owner.
  - `VortexBasic` remains the Phase 4 systems-and-passes validator, but it is
    not sufficient to close `04-06` after the migrated Async run under `cdb`
    surfaced debugger-visible D3D12 failures.
- Changed files this session:
  - `src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/PostRenderCleanup.cpp`
  - `Examples/Async/README.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererShell.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R "RendererCompositionQueue|CompositionPlanner|SceneRendererShell"` (fails on this branch because the broad regex still matches legacy `Oxygen.Renderer.*` `_NOT_BUILT` placeholders)
  - `ctest --preset test-debug --output-on-failure -R "^(Oxygen\.Vortex\.(RendererCompositionQueue|CompositionPlanner|SceneRendererShell)\.Tests|RendererCompositionQueueTest\.|CompositionPlannerTest\.|SceneRendererShellProofSurfaceTest\.)"`
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-vortexbasic Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererShell.Tests --parallel 4`
  - `rg -n "ResolveSceneColor|PostRenderCleanup|build/artifacts/vortex/phase-4/async/" Examples/Async/README.md design/vortex/IMPLEMENTATION-STATUS.md src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp src/Oxygen/Vortex/SceneRenderer/PostRenderCleanup.cpp`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-06`
  - `powershell -NoProfile -File tools/vortex/Verify-AsyncRuntimeProof.ps1 -CapturePath build/artifacts/vortex/phase-4/async/baseline_renderdoc.rdc`
  - `cdb.exe -G -g -logo build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.log -cf build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.commands.txt out/build-ninja/bin/Debug/Oxygen.Examples.Async.exe --frames 14 --fps 30 --vsync false --debug-layer true --capture-provider off`
- Result:
  - the composition queue tests now exercise the migrated single-view runtime
    publication/composition path instead of a raw texture shortcut
  - the planner proof now pins scene -> HUD -> ImGui ordering on the retained
    single-view path
  - the shell proof now locks Stage 13 and Stage 14 as reserved/inactive and
    verifies that the retained Stage 21 / Stage 23 ownership markers exist in
    source
  - the Async README now documents the shared proof-pack root, the Stage 21 /
    23 owners, the generated report files, the final-presentation keys, and
    the ImGui overlay audit keys
  - `Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-06`
    completed successfully on this branch
  - `Verify-AsyncRuntimeProof.ps1` passed again against
    `build/artifacts/vortex/phase-4/async/baseline_renderdoc.rdc`, preserving:
    `async_runtime_stage_order_valid=true`,
    `compositing_scope_present=true`,
    `stage22_tonemap_scope_present=true`,
    `final_present_nonzero=true`,
    `final_present_vs_tonemap_changed=true`
  - the reused `baseline_behaviors.md` still reports
    `imgui_runtime_registered: pass` and `demoshell_ui_draw_path: pass`
  - the debugger-backed Async audit wrote:
    - `build/artifacts/vortex/phase-4/async/04-06.debug-layer.transcript.log`
    - `build/artifacts/vortex/phase-4/async/04-06.debug-layer.report.txt`
  - that audit failed with:
    - `D3D12 ERROR #615 DEPTH_STENCIL_FORMAT_MISMATCH_PIPELINE_STATE` against `Vortex.DirectionalShadowSurface`
    - `D3D12 WARNING #821 CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE`
    - `D3D12 WARNING #1378 DRAW_POTENTIALLY_OUTSIDE_OF_VALID_RENDER_AREA`
    - no logged Async `exit code:` before the debugger terminated on the D3D12 break
- Code / validation delta:
  - The retained Stage 21 / 22 / 23 documentation and tests are stronger and
    the reused Async proof pack still validates structurally, but the
    debugger-backed migrated runtime path is not clean.
  - The authoritative Async proof pack remains
    `build/artifacts/vortex/phase-4/async/`; 04-06 revalidates it instead of
    hand-waving or duplicating it.
- Remaining blocker:
  - fix the migrated Async `cdb` audit failures on the Vortex path:
    - shadow draw uses a depth/stencil surface format that does not match the pipeline state
    - shadow draw viewport/scissor exceeds the smallest attached target extent
    - rerun the `04-06.debug-layer.*` audit and retain a clean Async `exit code:` before accepting any `04-06` closeout claim

### 2026-04-17 — Phase 4 Async bootstrap seam moved onto the Vortex runtime path

- Scope decision:
  - `04-05` is now complete for its owned seam boundary:
    Async no longer links or boots through the legacy renderer target.
  - The renderer-owned bootstrap/view-registration seam now lives in
    `src/Oxygen/Vortex/Renderer.h/.cpp` through public runtime view publish and
    composition helpers.
  - DemoShell/AppWindow runtime-UI migration still remains explicitly
    downstream work in `04-11`.
  - Async parity proof, harness proof, and composition/presentation closeout
    remain downstream work in `04-09` and `04-06`.
- Changed files this session:
  - `Examples/Async/CMakeLists.txt`
  - `Examples/Async/MainModule.h`
  - `Examples/Async/MainModule.cpp`
  - `Examples/Async/main_impl.cpp`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-async --parallel 4`
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-async --parallel 4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; rg -n "Oxygen/Renderer|oxygen::renderer" Examples/Async src/Oxygen/Vortex/Renderer.h src/Oxygen/Vortex/Renderer.cpp -g "!README.md"; if ($LASTEXITCODE -eq 0) { throw 'Legacy renderer references remain in Async bootstrap or Vortex renderer seam sources' }; if ($LASTEXITCODE -gt 1) { exit $LASTEXITCODE }; exit 0`
- Result:
  - `Examples/Async` now links `oxygen::vortex`
  - `main_impl.cpp` now registers `oxygen::vortex::Renderer` instead of the
    legacy renderer module and sets `RendererImplementation::kVortex`
  - `MainModule.cpp` now publishes the main runtime view and queues
    composition through Vortex-owned helpers instead of the legacy renderer
    bootstrap path
  - `Renderer.h/.cpp` now expose the public runtime seam that later
    DemoShell/AppWindow migration work is expected to consume
  - the post-migration source audit found no remaining
    `Oxygen/Renderer` / `oxygen::renderer` references in the owned Async
    bootstrap files or the Vortex seam sources
- Code / validation delta:
  - compile-time seam migration is proven by the Debug build of
    `oxygen-vortex` plus `oxygen-examples-async`
  - runtime/UI parity and proof artifacts are intentionally still open; this
    lane did not claim them
- Remaining blocker:
  - `04-11` must migrate DemoShell/AppWindow runtime and UI work onto the new
    seam
  - `04-09` must prove Async parity and harnesses on the migrated path
  - `04-06` must close composition/presentation validation

### 2026-04-17 — Phase 4 Async seam-migration baseline recorded before 04-05 code motion

- Scope decision:
  - `Examples/Async` remains the Phase 4 first-success integration gate.
  - The baseline artifact root for Async migration proof remains
    `build/artifacts/vortex/phase-4/async/`.
  - `04-05` owns only the initial seam replacement:
    legacy renderer linking, Async bootstrap ownership, and renderer-owned
    view-registration assumptions.
  - DemoShell/AppWindow runtime-UI migration and Async parity-proof capture
    remain downstream work in `04-11`, `04-09`, and `04-06`.
  - `VortexBasic` remains the durable Phase 4 systems-and-passes validator
    even after Async builds against Vortex.
- Changed files this session:
  - `Examples/Async/README.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Verification used for this session:
  - `rg -n "build/artifacts/vortex/phase-4/async/|legacy renderer|bootstrap|view-registration|VortexBasic" Examples/Async/README.md design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the Async README now names the Phase 4 artifact root and the owned seam
    replacements for the first migration lane
  - the implementation ledger now records the same seam truth and explicitly
    preserves the downstream runtime-UI and proof boundaries
- Code / validation delta:
  - no implementation code changed yet
  - build/test execution remains pending for the code-migration task
  - this ledger update exists to lock the Phase 4 migration truth before code
    motion starts
  - Remaining blocker:
    - `04-05` code migration still pending
    - `04-11`, `04-09`, and `04-06` remain deferred

### 2026-04-17 — Phase 4 Stage 15 runtime blocker closed on the live VortexBasic proof surface

- Scope decision:
  - `04-04` ownership/publication proof remains historically correct but is no
    longer treated as sufficient for `ENV-01` closure.
  - `04-05` and `04-06` stayed blocked until the strengthened `04-08`
    VortexBasic validator was green.
  - `VortexBasic` remains the durable Stage 15 systems-and-passes proof
    surface; `Examples/Async` remains the first-success integration gate.
- Changed files this session:
  - `tools/vortex/AnalyzeRenderDocVortexBasicCapture.py`
  - `tools/vortex/AnalyzeRenderDocVortexBasicProducts.py`
  - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1`
  - `tools/vortex/README.md`
  - `Examples/VortexBasic/MainModule.cpp`
  - `Examples/VortexBasic/main_impl.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/04-04-SUMMARY.md`
  - `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/04-VALIDATION.md`
- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-08`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-08; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; $report = 'build/artifacts/vortex/phase-4/vortexbasic/04-08.validation.txt'; if (!(Test-Path $report)) { throw 'Missing 04-08.validation.txt' }; $required = @('stage15_sky_draw_count_match=true', 'stage15_atmosphere_draw_count_match=true', 'stage15_fog_draw_count_match=true', 'stage15_sky_scene_color_changed=true', 'stage15_atmosphere_scene_color_changed=true', 'stage15_fog_scene_color_changed=true'); foreach ($entry in $required) { rg --fixed-strings --quiet $entry $report; if ($LASTEXITCODE -eq 1) { throw \"Missing required Stage 15 proof key: $entry\" }; if ($LASTEXITCODE -gt 1) { exit $LASTEXITCODE } }`
- Result:
  - the structural analyzer now emits explicit Stage 15 scope, draw-count, and
    ordered-execution proof keys for sky / atmosphere / fog
  - the product analyzer now emits per-pass Stage 15 `SceneColor` change keys
  - the assertion layer now fails if any Stage 15 pass disappears or becomes
    zero-output again
  - the live proof scene now exercises the environment lane through
    `kEnvironmentLighting` plus `with_atmosphere=true`
  - the sky pass required a runtime bug fix before blocker closure:
    the far-background mask now tolerates the live depth endpoint used by the
    VortexBasic capture instead of collapsing the sky pass to zero contribution
- Code / validation delta:
  - blocker-closing proof artifacts:
    - `build/artifacts/vortex/phase-4/vortexbasic/04-08.validation.txt`
    - `build/artifacts/vortex/phase-4/vortexbasic/04-08_frame4_vortexbasic_capture_report.txt`
    - `build/artifacts/vortex/phase-4/vortexbasic/04-08_frame4_products_report.txt`
    - `build/artifacts/vortex/phase-4/vortexbasic/04-08.debug-layer.report.txt`
  - key proof fields:
    - `analysis_result=success`
    - `overall_verdict=pass`
    - `runtime_exit_code=0`
    - `d3d12_error_count=0`
    - `stage15_sky_draw_count_match=true`
    - `stage15_atmosphere_draw_count_match=true`
    - `stage15_fog_draw_count_match=true`
    - `stage15_sky_scene_color_changed=true`
    - `stage15_atmosphere_scene_color_changed=true`
    - `stage15_fog_scene_color_changed=true`
- Remaining blocker:
  - the environment blocker is resolved
  - the next open Phase 4 work is the migration/integration lane in this order:
    `04-05` -> `04-11` -> `04-09` -> `04-06`

### 2026-04-17 - Phase 4 LLD remediation package completed

- Scope decision:
  - The Phase 4 tracked design package is now treated as complete and
    internally consistent for the current architecture and planned scope.
  - `DESIGN.md` remains the stable entry point and cross-subsystem rationale
    document.
  - Detailed authoritative contracts for Phase 4 now live in the remediated
    LLD package under `design/vortex/lld/`.
- Changed files this session:
  - `design/vortex/PLAN.md`
  - `design/vortex/DESIGN.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `design/vortex/lld/README.md`
  - `design/vortex/lld/phase4-lld-remediation-program.md`
  - `design/vortex/lld/lighting-service.md`
  - `design/vortex/lld/shadow-service.md`
  - `design/vortex/lld/environment-service.md`
  - `design/vortex/lld/post-process-service.md`
  - `design/vortex/lld/migration-playbook.md`
  - `design/vortex/lld/shadow-local-lights.md`
  - `design/vortex/lld/indirect-lighting-service.md`
- Verification used for this session:
  - targeted document rereads of the Phase 4 design package
  - stale-language / contradiction sweeps via `rg`
  - mandatory spawned-subagent review passes for:
    - design-gate truth
    - lighting-service parity / architecture
    - shadow-service parity / architecture
    - environment/post-process parity / architecture
    - final package coherence
- Result:
  - Phase 4 ordering/dependency truth is corrected:
    `LightingService` now defines the contracts that `ShadowService` and the
    environment publication / ambient-bridge boundary depend on, rather than
    the package claiming a blanket `4A-4D` parallel lane.
  - `LightingService` now has a truthful frame-scope Stage-6 contract,
    explicit directional-light authority, a complete forward-light publication
    package, and an explicit split between authoritative light state and
    service-owned proxy geometry.
  - `ShadowService` now carries a truthful directional-first Phase 4C contract
    with per-view publication and without freezing local-light/VSM storage
    choices into the Stage 4C binding seam.
  - `EnvironmentLightingService` now truthfully owns active Stage-15
    sky/atmosphere/fog composition, separates persistent probe state from
    per-view publication, and narrows the temporary Stage-12 ambient bridge to
    the explicit `EnvironmentAmbientBridgeBindings` payload.
  - `PostProcessService` now explicitly owns eye adaptation and future local
    exposure within Stage 22 while leaving current-view source resolution
    outside post ownership.
  - `migration-playbook.md` now describes the real legacy seam inventory for
    `Examples/Async`, the truthful Phase 4 feature baseline, and the explicit
    proof boundary that requires all four Phase 4 services to be live in the
    migrated run.
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run because this was a tracked design-package
    remediation session
  - verification evidence for this session is document review and subagent
    audit, not runtime/build proof
- Remaining blocker:
  - none for Phase 4 design readiness

### 2026-04-16 — SceneRenderer shell ownership aligned to Renderer-selected current-view execution

- Scope decision:
  - `Renderer Core` remains the authoritative owner of published runtime-view
    materialization and current-view selection / iteration.
  - `SceneRenderer` remains the owner of the scene-view stage chain for the
    current view selected in `RenderContext`.
  - UE5.7 iterates its `Views` array inside the scene renderer because it does
    not have a separate Renderer-Core layer; in Oxygen, keeping selection in
    `Renderer` is the cleaner boundary because `Renderer` already owns
    view registration, render-context materialization, per-view publication,
    and composition planning.
- Changed files this session:
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `design/vortex/ARCHITECTURE.md`
  - `design/vortex/lld/scene-renderer-shell.md`
  - `design/vortex/lld/init-views.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/PHASE3-CLOSURE-GAPS.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-item11-runtime-validation`
- Result:
  - architecture and LLD surfaces now agree with the stable current layering:
    - `Renderer` materializes eligible frame views and selects the current
      scene-view cursor
    - `SceneRenderer` consumes that current view and executes the stage chain
  - code comments at the ownership seam now state the same contract directly in:
    - `Renderer::PopulateRenderContextViewState(...)`
    - `SceneRenderer::OnRender(...)`
- Evidence retained:
  - focused proof:
    - `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
    - `Oxygen.Vortex.SceneRendererPublication.Tests`
  - live D3D12 proof:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-item11-runtime-validation.validation.txt`

### 2026-04-16 — Stage 9/10 scene-texture validity boundary aligned to Stage 10

- Scope decision:
  - `SceneColor` plus the active GBuffers remain **Stage 10-owned valid
    products** in Vortex.
  - Stage 9 remains the raw attachment-write point for those products and the
    output-backed publication point for `SceneVelocity`.
  - UE5.7 does not expose a separate top-level Stage 10 seam, but Vortex does;
    in Vortex, “valid” therefore means “consumable through the canonical
    `SceneTextureBindings` / `ViewFrameBindings` publication stack,” which is
    first true after the Stage 10 rebuild/refresh boundary.
- Changed files this session:
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `design/vortex/ARCHITECTURE.md`
  - `design/vortex/PLAN.md`
  - `design/vortex/lld/scene-textures.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/PHASE3-CLOSURE-GAPS.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-item10-runtime-validation`
- Result:
  - architecture, plan, and scene-textures LLD now agree with the existing
    implementation and proof surface:
    - Stage 9 writes raw base-pass attachments
    - Stage 10 is the first consumable publication boundary for `SceneColor`
      plus the active GBuffers
  - `SceneRenderer` now carries an explicit guard that prevents Stage 9 from
    silently publishing `SceneColor` / GBuffer bindings before the Stage 10
    rebuild boundary
  - the deferred-core proof name now states the boundary directly:
    - `BasePassLeavesSceneColorAndGBuffersInvalidUntilStage10`
- Evidence retained:
  - focused proof:
    - `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
    - `Oxygen.Vortex.SceneRendererPublication.Tests`
  - live D3D12 proof:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-item10-runtime-validation.validation.txt`

### 2026-04-16 — Phase 03 deferred debug visualization runtime route landed

- Changed files this session:
  - `src/Oxygen/Vortex/ShaderDebugMode.h`
  - `src/Oxygen/Vortex/RenderContext.h`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
  - `Examples/VortexBasic/MainModule.h`
  - `Examples/VortexBasic/MainModule.cpp`
  - `Examples/VortexBasic/main_impl.cpp`
  - `tools/vortex/AnalyzeRenderDocVortexBasicDebugCapture.py`
  - `tools/vortex/Assert-VortexBasicDebugViewProof.ps1`
  - `tools/vortex/Verify-VortexBasicDebugViewProof.ps1`
  - `tools/vortex/Run-VortexBasicDebugViewValidation.ps1`
  - `design/vortex/PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/PHASE3-CLOSURE-GAPS.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  - `cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererDeferredCore|CompositionPlanner)\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-debug-remediation-baseline`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicDebugViewValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-debug-view-validation`
- Result:
  - Vortex now exposes a public `ShaderDebugMode` contract and threads the
    selected mode from `Renderer` into `RenderContext`.
  - `SceneRenderer` now owns an actual deferred debug-visualization route fed
    by Stage 10 scene products and writing into `SceneColor`.
  - Supported live deferred debug views are:
    - base color
    - world normals
    - roughness
    - metalness
    - scene-depth-raw
    - scene-depth-linear
  - `BasePassDebugView.hlsl` now uses the standard `ViewConstants ->
    ViewFrameBindings` path instead of a debug-only root-constant slot.
  - `VortexBasic` now exposes `--shader-debug-mode` and the validation scene
    carries nonzero metallic content so the metallic view is truthfully
    inspectable.
- Evidence retained:
  - baseline runtime proof:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-debug-remediation-baseline.validation.txt`
  - deferred debug-view proof suite:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-debug-view-validation.validation.txt`
- Remaining runtime log noise:
  - `TextureBinder initialized with error texture`
  - first-frame compositing fallback to `VortexBasic.SceneColor`
  - `ScriptCompilationService ... compile failed : 0`

### 2026-04-16 — Phase 03 closure accepted for the current engine feature envelope

- Scope decision:
  - Phase 03 items 6, 7, and 8 are now treated as `done` for the maximum
    functional extent possible with the current engine features.
  - Deferred skinned / morph engine enablement is recorded as explicit TODO
    carry-forward work and no longer blocks Phase 03 closure.
- Changed files this session:
  - `src/Oxygen/Data/Vertex.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vertex.hlsli`
  - `src/Oxygen/SceneSync/RuntimeMotionProducerModule.h`
  - `src/Oxygen/Vortex/Internal/DeformationHistoryCache.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VELOCITY-PARITY-TRACKER.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-examples-vortexbasic Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.SceneRendererDeferredCore\\.Tests$" --output-on-failure`
  - existing live D3D12 closure proof retained:
    - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-active-fixed2`
- Result:
  - closure state is now explicit and scoped:
    - Item 6: done for the current engine feature envelope
    - Item 7: done for the current engine feature envelope
    - Item 8: done for the current engine feature envelope
  - concise code comments now mark the real future extension points for
    skinned / morph support in:
    - CPU vertex ABI
    - shader vertex ABI
    - SceneSync placeholder producer bridge
    - renderer-owned deformation history
- Evidence retained for closure:
  - focused proof:
    - `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
  - live D3D12 proof:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-active-fixed2.validation.txt`
  - key runtime fields:
    - `overall_verdict=pass`
    - `stage9_has_expected_targets=true`
    - `stage9_velocity_nonzero=true`
    - `stage12_point_scene_color_nonzero=true`
    - `stage12_spot_scene_color_nonzero=true`
    - `final_present_nonzero=true`
- Deferred carry-forward:
  - full skinned / morph velocity parity remains future engine-enablement work,
    but it is now documented as TODO carry-forward rather than a Phase 03
    blocker

### 2026-04-16 — Phase 03 MVWO chain proved live; remaining blocker narrowed to missing skinned/morph substrate

- Changed files this session:
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h`
  - `src/Oxygen/Vortex/Test/Fakes/Graphics.h`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `Examples/VortexBasic/MainModule.cpp`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VELOCITY-PARITY-TRACKER.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-examples-vortexbasic --parallel 4`
  - `cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.SceneRendererDeferredCore\\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-active-fixed2`
- Result:
  - the Stage-9 MVWO auxiliary path is now active on the default live `VortexBasic` runtime surface instead of only existing as dormant infrastructure
  - the real regression behind the first activation failure was fixed:
    - the auxiliary pass no longer clears depth through the D3D12 framebuffer helper path
    - local-light Stage 12 proof remains nonzero with MVWO/WPO active
  - focused test coverage now proves the Stage-9 chain shape on the fake renderer path:
    - one velocity copy
    - one auxiliary draw
    - one merge dispatch
    - color-only aux clear surface (no depth attachment on the aux clear framebuffer)
- Code / validation delta:
  - focused proof:
    - `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
  - live D3D12 proof:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-active-fixed2.validation.txt`
  - key runtime fields:
    - `overall_verdict=pass`
    - `stage9_has_expected_targets=true`
    - `stage9_velocity_nonzero=true`
    - `stage12_point_scene_color_nonzero=true`
    - `stage12_spot_scene_color_nonzero=true`
  - capture proof now contains:
    - `Vortex.Stage9.BasePass.VelocityAux`
    - `Vortex.Stage9.BasePass.VelocityMerge`
    - `ID3D12GraphicsCommandList::Dispatch()`
- Remaining blocker:
  - Item 6 remains open because MVWO merge correctness is only structurally tested so far; producer-specific final-scene proof and broader correctness cases are still missing
  - Item 7 remains open because the authoritative validation scene still lacks masked, skinned, and morph/deformation producers
  - the hard blocker for Items 7 and 8 is now explicit in repo code:
    - `src/Oxygen/Data/Vertex.h` still documents skin weights / bone indices as future extension
    - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vertex.hlsli` still exposes only rigid vertex attributes
    - Vortex currently publishes skinned/morph slots, but there is still no live geometry upload / shader fetch path for real joint-weight or morph streams
  - that missing render substrate prevents truthful closure of full masked/deformed/skinned/WPO parity today

#### TODO — Skinned / Morph Support Once Engine Runtime Exists

- extend the live vertex ABI in:
  - `src/Oxygen/Data/Vertex.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vertex.hlsli`
  so real skinning and morph stream fetch is possible instead of placeholder publication only
- teach `GeometryUploader` / scene-prep geometry references to publish the required skinned and morph stream bindings:
  - joint indices
  - joint weights
  - inverse bind data
  - joint remap data
  - morph target / deformation streams
- connect the future engine-side animation / skeleton runtime to `SceneSync::RuntimeMotionProducerModule` so it freezes:
  - authoritative current joint-palette state
  - authoritative current morph-weight / deformation state
  at `kPublishViews`
- extend `InitViews` and renderer-owned publication/history to materialize truthful current/previous families for:
  - `SkinnedPosePublication`
  - `MorphPublication`
  including invalidation on skeleton layout / morph layout / geometry identity change
- update Stage 3 / Stage 9 shaders to evaluate:
  - current skinned deformation
  - previous skinned deformation
  - current morph deformation
  - previous morph deformation
  before velocity generation, not as disconnected metadata-only flags
- add focused unit coverage for:
  - skinned publication roll-forward
  - morph publication roll-forward
  - skeleton layout invalidation
  - morph layout invalidation
  - first-frame / invalid-history fallback for both families
- expand `VortexBasic` with deterministic producer-specific proof regions for:
  - skinned velocity
  - morph/deformation velocity
  and keep those captures in the live D3D12 validation pack before any closure claim

### 2026-04-16 — Phase 03 MVWO auxiliary infrastructure landed; default runtime proof restored

- Changed files this session:
  - `src/Oxygen/SceneSync/RuntimeMotionProducerModule.h`
  - `src/Oxygen/SceneSync/RuntimeMotionProducerModule.cpp`
  - `src/Oxygen/SceneSync/Test/RuntimeMotionProducerModule_test.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
  - `src/Oxygen/Vortex/Test/SceneTextures_test.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/DrawHelpers.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/VelocityPublications.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassVelocityAux.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassVelocityMerge.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/DepthPrepass/DepthPrepass.hlsl`
  - `tools/vortex/AnalyzeRenderDocVortexBasicCapture.py`
  - `tools/vortex/AnalyzeRenderDocVortexBasicProducts.py`
  - `tools/vortex/Assert-VortexBasicRuntimeProof.ps1`
  - `Examples/VortexBasic/MainModule.cpp`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VELOCITY-PARITY-TRACKER.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-scenesync Oxygen.SceneSync.RuntimeMotionProducerModule.Tests oxygen-vortex Oxygen.Vortex.SceneRendererDeferredCore.Tests oxygen-examples-vortexbasic --parallel 4`
  - `cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^(Oxygen\.SceneSync\.RuntimeMotionProducerModule|Oxygen\.Vortex\.(SceneTextures|SceneRendererPublication|SceneRendererDeferredCore|DeformationHistoryCache))\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-baseline-restored`
- Result:
  - `SceneSync::RuntimeMotionProducerModule` now exposes a real runtime material-motion input contract:
    - producer-owned current WPO parameter block publication
    - producer-owned current MVWO parameter block publication
    - frozen snapshot overlay at `kPublishViews`
  - `InitViews` now preserves those parameter blocks in renderer-owned current/previous typed publications instead of publishing capability bits only.
  - Stage 3 and Stage 9 shaders now consume current/previous material-WPO publications so visible geometry can follow the same renderer-owned motion contract that velocity uses.
  - Stage 9 now owns explicit MVWO infrastructure:
    - stage-local `VelocityBasePassCopy`
    - stage-local `VelocityMotionVectorWorldOffset`
    - `BasePassVelocityAux.hlsl`
    - `BasePassVelocityMerge.hlsl`
    - velocity UAV publication through `SceneTextureBindings`
    - nested GPU scopes:
      - `Vortex.Stage9.BasePass.MainPass`
      - `Vortex.Stage9.BasePass.VelocityAux`
      - `Vortex.Stage9.BasePass.VelocityMerge`
  - RenderDoc analyzers now treat `Stage9.MainPass` as the authoritative GBuffer proof surface, so future auxiliary/merge draws do not corrupt the base-pass validation report.
  - The strengthened default runtime proof is green on:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-baseline-restored.validation.txt`
- Code / validation delta:
  - focused unit/integration coverage is green after the slice:
    - `Oxygen.SceneSync.RuntimeMotionProducerModule.Tests`
    - `Oxygen.Vortex.SceneTextures.Tests`
    - `Oxygen.Vortex.SceneRendererPublication.Tests`
    - `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
    - `Oxygen.Vortex.DeformationHistoryCache.Tests`
  - the default live D3D12 proof remains green with:
    - `analysis_result=success`
    - `overall_verdict=pass`
    - `stage9_has_expected_targets=true`
    - `stage9_velocity_nonzero=true`
    - `stage12_point_scene_color_nonzero=true`
    - `stage12_spot_scene_color_nonzero=true`
  - a non-default MVWO activation attempt on the rotating cube proved the new producer chain executes:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-mvwo-active_frame4_vortexbasic_capture_report.txt`
    - capture evidence includes:
      - `Vortex.Stage9.BasePass.VelocityAux`
      - `ID3D12GraphicsCommandList::CopyTextureRegion()`
      - `Vortex.Stage9.BasePass.VelocityMerge`
      - `ID3D12GraphicsCommandList::Dispatch()`
  - that same activation attempt was intentionally not left as the default runtime path because point/spot local-light product proof dropped to zero on the current simple scene; the authoritative validation-scene expansion is therefore still open and required.
- Remaining blocker:
  - Item 6 is not closed yet:
    - MVWO auxiliary/merge infrastructure exists, but it still lacks authoritative runtime proof on the final validation scene
    - focused merge-correctness tests are still missing
  - Item 7 is not closed yet:
    - `VortexBasic` has not yet been expanded into the approved authoritative producer scene for masked / skinned / morph / WPO coverage
    - the current default scene is intentionally still the pre-expansion two-draw baseline
  - Item 8 is not closed yet:
    - docs are synced only to the current in-progress state
    - final review/remediation has not been run
    - skinned and morph/deformation runtime producers are still absent
  - items 6, 7, and 8 remain open

### 2026-04-16 — Phase 03 Stage-2 motion-publication bridge, previous-view publication seam, and runtime regressions advanced

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/PreparedSceneFrame.h`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h`
  - `src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp`
  - `src/Oxygen/Vortex/Resources/TransformUploader.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Internal/DeformationHistoryCache_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
  - `src/Oxygen/Vortex/Types/DrawFrameBindings.h`
  - `src/Oxygen/Vortex/Types/VelocityPublications.h`
  - `src/Oxygen/Vortex/Types/ViewHistoryFrameBindings.h`
  - `src/Oxygen/Vortex/Internal/DeformationHistoryCache.h`
  - `src/Oxygen/Vortex/Internal/DeformationHistoryCache.cpp`
  - `src/Oxygen/Vortex/Internal/PreviousViewHistoryCache.h`
  - `src/Oxygen/Vortex/Internal/PreviousViewHistoryCache.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/DrawFrameBindings.hlsli`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VELOCITY-PARITY-TRACKER.md`
- Commands used for verification:
  - `out/build-ninja/bin/Debug/Oxygen.Vortex.DeformationHistoryCache.Tests.exe`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.(SceneRendererDeferredCore|DeformationHistoryCache)\\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-basepass-result`
- Result:
  - Stage 2 no longer leaves the SceneSync producer bridge unused:
    - `InitViewsModule` now consumes `RuntimeMotionProducerModule` snapshots
    - `PreparedSceneFrame` now carries explicit typed current/previous motion-publication slots for:
      - material WPO
      - motion-vector status
      - per-draw velocity publication metadata
    - renderer-owned render-identity history now rolls current/previous material-motion and motion-vector-status publications through `DeformationHistoryCache`
  - draw-order publication mapping is now stable against downstream emitter sorting because `DrawMetadataEmitter` preserves per-draw render identity alongside sorted draw metadata
  - renderer-owned previous-view history now exists and is published through the already-owned per-view `history_frame_slot` seam instead of expanding the shared `ViewConstants` ABI
  - the Stage 9 bool seam is partially removed:
    - `BasePassModule::Execute(...)` now returns `BasePassExecutionResult`
    - `SceneRenderer` Stage 9 / 10 gating now consumes that result object directly
  - two runtime regressions on the live D3D12 path were fixed:
    - first-frame compositing now falls back to the published composite source when the resolved scene-color artifact is not ready yet
    - `TransformUploader` now arms `previous_worlds_buffer_` on frame start, eliminating the invalid-slot previous-world upload failure
  - fresh live `VortexBasic` proof remains green after the slice
- Code / validation delta:
  - fresh D3D12 proof artifact:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-basepass-result.validation.txt`
  - key proof fields:
    - `analysis_result=success`
    - `overall_verdict=pass`
    - `phase03_runtime_stage_order_match=true`
    - `stage3_depth_ok=true`
    - `stage9_gbuffer_base_color_nonzero=true`
    - `stage12_directional_scene_color_nonzero=true`
    - `stage12_point_scene_color_nonzero=true`
    - `stage12_spot_scene_color_nonzero=true`
    - `final_present_nonzero=true`
  - new focused cache lifecycle proof is green:
    - `Oxygen.Vortex.DeformationHistoryCache.Tests`
- Remaining blocker:
  - the velocity producer chain is still incomplete:
    - Base pass does not yet bind/write a real velocity MRT from current/previous typed motion inputs
    - MVWO auxiliary pass + merge/update is still absent
    - previous-view data is published but not yet consumed by Stage 9 shaders
    - skinned / morph current-state runtime payloads are still truthful placeholders or absent because the upstream runtimes do not exist yet
  - `Oxygen.Vortex.SceneRendererPublication.Tests` currently crashes with an SEH teardown failure and remains a verification gap on this branch
  - items 6, 7, and 8 remain open

### 2026-04-16 — Phase 03 SceneSync producer slice rerun on live D3D12 VortexBasic validation

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VELOCITY-PARITY-TRACKER.md`
- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-scenesync`
- Result:
  - the primary validation path for the current SceneSync producer slice was rerun through the live D3D12 `VortexBasic` runtime proof flow rather than headless smoke
  - the runtime proof passed on the new artifact path
- Code / validation delta:
  - validation artifact:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/phase3-runtime-validation-scenesync.validation.txt`
  - key proof fields:
    - `analysis_result=success`
    - `overall_verdict=pass`
    - `phase03_runtime_stage_order_match=true`
    - `stage3_depth_ok=true`
    - `stage9_gbuffer_base_color_nonzero=true`
    - `stage12_directional_scene_color_nonzero=true`
    - `stage12_point_scene_color_nonzero=true`
    - `stage12_spot_scene_color_nonzero=true`
    - `final_present_nonzero=true`
- Remaining blocker:
  - this D3D12 rerun validates the current runtime branch, but it does not close items 6, 7, or 8 because the Stage-2 consumer wiring, deformation history/publication, previous-view history, Stage-9 output-backed velocity path, MVWO merge path, and final review/remediation loop are still incomplete

### 2026-04-16 — Phase 03 runtime motion producer bridge stood up, verified, and kept open truthfully

- Changed files this session:
  - `src/Oxygen/Core/EngineModule.h`
  - `src/Oxygen/Engine/ModuleManager.cpp`
  - `src/Oxygen/SceneSync/CMakeLists.txt`
  - `src/Oxygen/SceneSync/RuntimeMotionProducerModule.h`
  - `src/Oxygen/SceneSync/RuntimeMotionProducerModule.cpp`
  - `src/Oxygen/SceneSync/Test/CMakeLists.txt`
  - `src/Oxygen/SceneSync/Test/RuntimeMotionProducerModule_test.cpp`
  - `Examples/VortexBasic/CMakeLists.txt`
  - `Examples/VortexBasic/main_impl.cpp`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VELOCITY-PARITY-TRACKER.md`
- Commands used for verification:
  - `cmake --build out/build-ninja --config Debug --target oxygen-scenesync Oxygen.SceneSync.RuntimeMotionProducerModule.Tests oxygen-examples-vortexbasic --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.SceneSync\\.RuntimeMotionProducerModule\\.Tests$" --output-on-failure`
  - `out/build-ninja/bin/Debug/Oxygen.Examples.VortexBasic.exe --frames 1 --headless`
- Result:
  - `SceneSync` now owns a real `RuntimeMotionProducerModule` that freezes an immutable per-scene current-state motion snapshot at `kPublishViews`.
  - The snapshot currently publishes:
    - stable material-motion keys keyed by `NodeHandle + GeometryAssetKey + LOD + Submesh`
    - resolved material contract hashes
    - truthful WPO / MVWO / temporal-responsiveness / pixel-animation status bits, which remain false/absent until a real runtime material-motion producer exists
    - truthful skinned-family records that explicitly carry `has_runtime_pose=false` when skinned meshes are present
  - `VortexBasic` now instantiates the module on the real runtime path, and the earlier headless one-frame smoke run remained a secondary bring-up check only
  - Focused unit coverage now proves the snapshot publication seam and material-override key-stability / contract-change behavior.
- Code / validation delta:
  - no Phase 03 closure item is claimed closed from this slice
  - no Stage-9 velocity, MVWO auxiliary path, deformation-history, or previous-view history claims are made from this slice
- Remaining blocker:
  - Phase 03 still lacks:
    - Vortex consumption of the new SceneSync snapshot during Stage 2
    - renderer-owned deformation-history/current-previous publication families
    - previous-view history
    - output-backed Stage-9 velocity production
    - MVWO auxiliary pass + merge/update
    - expanded runtime validation surface and final review/remediation loop
  - items 6, 7, and 8 remain open until those code paths, docs, tests, runtime proof, and final review all pass

### 2026-04-16 — Phase 03 velocity redesign approved and first implementation slice landed

- Changed files this session:
  - `design/vortex/ARCHITECTURE.md`
  - `design/vortex/PLAN.md`
  - `design/vortex/lld/base-pass.md`
  - `design/vortex/lld/depth-prepass.md`
  - `design/vortex/lld/init-views.md`
  - `design/vortex/lld/sceneprep-refactor.md`
  - `design/vortex/lld/shader-contracts.md`
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Internal/RigidTransformHistoryCache.h`
  - `src/Oxygen/Vortex/Internal/RigidTransformHistoryCache.cpp`
  - `src/Oxygen/Vortex/PreparedSceneFrame.h`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/Resources/TransformUploader.h`
  - `src/Oxygen/Vortex/Resources/TransformUploader.cpp`
  - `src/Oxygen/Vortex/ScenePrep/Extractors.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepState.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `src/Oxygen/Vortex/Types/DrawFrameBindings.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/DrawFrameBindings.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Depth/DepthPrePass.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Forward/ForwardMesh_VS.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/DepthPrepass/DepthPrepass.hlsl`
- Commands used for verification:
  - two architecture review subagents over the revised design package
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.SceneRendererDeferredCore\\.Tests$" --output-on-failure`
- Result:
  - the Phase 03 design package was revised to target full UE5.7-grade opaque velocity parity, explicitly separate renderer motion vectors from Physics-module velocities, and was re-reviewed to unconditional architecture approval
  - the first code slice landed:
    - renderer-owned rigid transform history cache
    - current/previous transform publication seam through `TransformUploader`, `PreparedSceneFrame`, `DrawFrameBindings`, and `Renderer` publication
    - Stage 9 masked base-pass pipeline permutation selection now drives `ALPHA_TEST` instead of silently treating masked draws as opaque
  - targeted deferred-core tests remain green after the slice
- Code / validation delta:
  - no Phase 03 closure item is claimed closed from this slice
  - no live runtime validation rerun was claimed yet because the full opaque velocity producer chain is still incomplete
- Remaining blocker:
  - full code implementation is still open for:
    - previous-view history publication/runtime use
    - output-backed Stage 9 velocity production
    - motion-vector-world-offset auxiliary path + merge/update
    - deformation/skinned/WPO producer inputs and validation-surface expansion
  - items 6, 7, and 8 remain open until those code paths, tests, docs, and runtime proof are complete

### 2026-04-16 — Phase 03 point/spot local-light product gate closure synced

- Changed files this session:
  - `.planning/workstreams/vortex/phases/03-deferred-core/PHASE3-CLOSURE-GAPS.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n "diagnostic-only|diagnostic only|point/spot.*SceneColor|durable runtime gate" design/vortex/IMPLEMENTATION-STATUS.md .planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md tools/vortex/README.md tools/vortex/Assert-VortexBasicRuntimeProof.ps1 tools/vortex/AnalyzeRenderDocVortexBasicProducts.py`
  - `cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic --parallel 4`
  - `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output out/build-ninja/analysis/vortex/vortexbasic/runtime/task21-05-pointspot-product-gate`
- Result:
  - The active Phase 03 validator, `tools/vortex/README.md`, and the implementation ledger now tell the same truth for Stage 12 products: point/spot nonzero `SceneColor` is part of the durable live-runtime gate, not a diagnostic-only signal.
  - Fresh VortexBasic runtime proof passed on the item-5 artifact path and preserved the current structural + product gate.
- Code / validation delta:
  - no runtime code changed in this remediation slice
  - fresh proof artifact: `out/build-ninja/analysis/vortex/vortexbasic/runtime/task21-05-pointspot-product-gate.validation.txt`
  - fresh proof result: `analysis_result=success`, `overall_verdict=pass`, `stage12_point_scene_color_nonzero=true`, `stage12_spot_scene_color_nonzero=true`
- Remaining blocker:
  - final multi-subagent architectural/LLD/code-quality review plus remediation of the remaining open closure-gap items before any Phase 03 closure claim

### 2026-04-16 — Phase 3 deferred-core closeout blocked

- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - The Phase 03 closeout gate did not pass.
  - Report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Missing delta: stage_2_order=fail, stage_12_order=fail, stencil_local_lights=fail
- RenderDoc runtime validation remains deferred to Phase 04 and is not part of this failure.

### 2026-04-16 — Historical 03-15 frame-10 closeout pack failed and is now superseded

- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - The historical 03-15 frame-10 closeout gate did not pass.
  - Report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Missing delta: stage_2_order=fail, stage_3_order=fail, stage_9_order=fail, stage_12_order=fail, gbuffer_contents=fail, scene_color_lit=fail, stencil_local_lights=fail
- Status of this evidence:
  - This frame-10 closeout pack is no longer the authoritative Phase 03 closure surface.
  - The current authoritative runtime proof path is the live `VortexBasic` validation flow (`tools/vortex/Run-VortexBasicRuntimeValidation.ps1`).
  - Do not use this historical frame-10 result as the current Phase 03 closure verdict.

### 2026-04-16 — Phase 03 cleanup lane docs synced to the retained runtime branch

- Changed files this session:
  - `design/vortex/lld/scene-renderer-shell.md`
  - `design/vortex/lld/base-pass.md`
  - `design/vortex/lld/scene-textures.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - repo-grounded doc/code comparison against the retained `03-21` runtime branch
  - latest cleanup-lane proof artifact review:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/task21-04-logcleanup-proof.validation.txt`
- Result:
  - the LLDs now describe the retained Stage 10 republish boundary, the early-Z color-only clear rule, and the explicit Stage 21 resolved-artifact handoff used by composition
  - the Phase 03 validation docs now describe the current VortexBasic wrapper truthfully: structural + product analyzers together, with point/spot product checks treated as part of the durable runtime gate
  - the implementation ledger now matches the cleanup-lane reality instead of the earlier blocked-state assumptions alone
- Code / validation delta:
  - no new implementation code changed in this documentation-sync session
  - the latest cleanup-lane runtime proof remains green on the current build, but Phase 03 is still open pending the remaining cleanup-lane task(s) and the final multi-review gate
- Remaining blocker:
  - finish the remaining `03-21` work, then run the required final multi-subagent Phase 03 review/remediation loop before any closure claim

### 2026-04-16 — Phase 03 cleanup lane added after the live-runtime remediation checkpoint

- Changed files this session:
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-21-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `git commit -m "chore: preserve the current Phase 03 live-runtime remediation snapshot" ...`
  - repo-grounded staged-tree review against the current `03-18` through `03-20` remediation branch
- Result:
  - The full live-runtime remediation branch is preserved in temporary checkpoint commit `5a3926cd3`.
  - Phase 03 now carries an explicit `03-21` cleanup lane so the retained fixes can be cleaned without sacrificing the working `VortexBasic` scene or the RenderDoc proof surface.
  - The RenderDoc analyzers are preserved by design; the cleanup lane treats them as durable proof assets that need consolidation and hardening rather than deletion.
- Code / validation delta:
  - no new runtime or build verification was claimed from this planning-only update
  - `03-21` now defines the required per-remediation proof gate: scene still renders, VortexBasic RenderDoc proof stays green, and code review approves before commit
- Remaining blocker:
  - execute `03-18` through `03-21` truthfully before Phase 03 can be treated as clean or closed

### 2026-04-15 — Phase 03 returned to blocked gap-closure status after live runtime validation

- Changed files this session:
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-18-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-19-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-20-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - debugger-assisted `VortexBasic` runtime validation with D3D12 debug-layer error/call-stack capture
  - review intake from the `Lagrange` audit lane over resource-state / registration / lifetime management
- Result:
  - Phase 03 can no longer be claimed complete.
  - Live `VortexBasic` validation exposed barrier mismatches on persistent scene textures, proving that deferred-core stages are still guessing incoming resource states instead of consuming an authoritative graphics-layer state contract.
  - The Phase 03 roadmap now carries a three-plan gap-closure track:
    - `03-18` tracker-centered persistent state continuity
    - `03-19` deterministic registration / framebuffer-view ownership / GPU-safe retirement
    - `03-20` live runtime D3D12 + capture-backed proof closure
  - Runtime proof is no longer deferred to Phase 04 because `VortexBasic` now exists as the live Phase 03 validation surface.
- Code / validation delta:
  - no implementation code was claimed complete from this planning correction
  - non-runtime deferred-core tests remain green, but runtime deferred-core proof is still blocked
- Remaining blocker:
  - execute `03-18` through `03-20` and do not reopen Phase 04 until the live runtime proof is clean

### 2026-04-15 — Phase 3 remediation closed with real Stage 3/9/12 rendering and proof rerun

- Changed files this session:
  - `src/Oxygen/Graphics/Common/Internal/FramebufferImpl.cpp`
  - `src/Oxygen/Graphics/Common/Internal/ToStringConverters.cpp`
  - `src/Oxygen/Graphics/Common/PipelineState.cpp`
  - `src/Oxygen/Graphics/Common/PipelineState.h`
  - `src/Oxygen/Graphics/Direct3D12/Detail/Converters.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `tools/vortex/AnalyzeDeferredCoreCapture.py`
  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-17-SUMMARY.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.(SceneRendererDeferredCore|SceneRendererPublication)\\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
- Result:
  - Stage 3 now records a real depth/partial-velocity pass and copies SceneDepth into PartialDepth.
  - Stage 9 now records a real base pass and keeps depth testing aligned with the active reverse-Z convention.
  - Stage 12 now records actual directional fullscreen deferred draws plus one-pass bounded-volume point/spot light draws, publishes per-light constants, and uses procedural local-light volume generation.
  - The deferred-core/publication suites pass, and the repo-owned closeout runner produces a passing report again with the revised render-path checks.
- Code / validation delta:
  - `DEFR-01` is now satisfied in code and proof.
  - `DEFR-02` is now satisfied in code and proof.
  - Runtime RenderDoc capture remains intentionally deferred to Phase 04, but it is no longer a Phase 03 blocker.
- Remaining blocker:
  - None for Phase 03 exit. Resume with Phase 04 planning and the first migrated runtime surface.

### 2026-04-15 — Phase 3 deferred-core proof pack

- Changed files this session:
  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`
  - `tools/vortex/AnalyzeDeferredCoreCapture.py`
  - `tools/vortex/Analyze-DeferredCoreCapture.ps1`
  - `tools/vortex/Assert-DeferredCoreCaptureReport.ps1`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - Phase 3 closeout is proven by source/test/log-backed analysis of the current deferred-core tree.
  - Historical note from the old frame-10 closeout pack: at that time runtime
    validation was still deferred to Phase 04.
  - Analyzer report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Code / validation delta:
  - The proof pack now closes Stage 2/3/9/12 ordering, GBuffer publication, SceneColor accumulation, and bounded-volume local-light behavior without claiming a runtime capture that does not exist yet.
- Remaining blocker:
  - That historical note is now superseded by the current design package:
    the Phase 4 first-success gate is `Examples/Async` per `PLAN.md`, while
    the live `VortexBasic` runtime validation path already exists for the Phase
    3 retained branch.

### 2026-04-15 — Phase 3 deferred-core closeout blocked

- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - The Phase 03 closeout gate did not pass.
  - Report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Missing delta: stage_2_order=fail, stage_3_order=fail, stage_9_order=fail, stage_12_order=fail, gbuffer_contents=fail, scene_color_lit=fail, stencil_local_lights=fail
- RenderDoc runtime validation remains deferred to Phase 04 and is not part of this failure.

### 2026-04-15 — Phase 3 completion claim invalidated by post-execution review

- Changed files this session:
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-15-SUMMARY.md`
  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`
- Commands used for verification:
  - advisory review over the final Phase 03 range
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererDeferredCore.Tests.exe`
  - `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererPublication.Tests.exe`
  - `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp -Configuration Debug -IncludeTests -SummaryOnly`
- Result:
  - all 15 Phase 03 plans were executed, but the phase must not be treated as complete
  - review found that Stage 12 still proves telemetry rather than truthful SceneColor accumulation / bounded-volume local-light behavior
  - review found that the current closeout analyzer overstates key claims through token/name checks instead of artifact/state-level proof
  - review found that the current velocity-completion claim is still a shell seam, not a proven skinned/WPO path
  - the closeout runner now rebuilds the Vortex test executables before `ctest`, but that does not remove the larger Phase 03 blockers
- Code / validation delta:
  - Phase 03 is now explicitly `blocked` again
  - Phase 04 remains locked until review remediation is planned and landed
  - `STATE.md` and `ROADMAP.md` are now brought under managed repo state and reflect the real branch status
- Remaining blocker:
  - plan and implement the Phase 03 remediation work that closes the blocking review findings truthfully

### 2026-04-15 — Phase 3 execution advanced through 03-05 and the ledger now resumes at 03-06

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp`
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-03-SUMMARY.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-04-SUMMARY.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-05-SUMMARY.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/REQUIREMENTS.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererShell.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R SceneRendererShell`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)" --output-on-failure`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererShell.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "Oxygen\.Vortex\.(SceneRendererShell|SceneRendererDeferredCore|SceneRendererPublication)" --output-on-failure`
- Result:
  - `03-03` turned Stage 2 into a real `InitViewsModule` with persistent ScenePrep state and per-view prepared-scene storage.
  - `03-04` added a dedicated deferred-core proof surface and forced the missing Stage 2 active-view prepared-frame rebinding fix in `SceneRenderer.cpp`.
  - `03-05` turned Stage 3 into a real `DepthPrepassModule` shell and propagated `depth_prepass_completeness` through `RenderContext.current_view`.
  - the focused shell/publication/deferred-core verification set is green, and the vortex workstream ledger now resumes at `03-06`.
- Code / validation delta:
  - implementation code changed materially in the renderer shell, Stage 2, Stage 3, and the deferred-core proof surface
  - validation evidence exists for shell/publication/deferred-core test coverage, but there is still no proof yet for real depth-prepass draw processing, Stage 3 publication products, base-pass output, or deferred lighting
  - Phase 3 is therefore `in_progress`, not `done`
- Remaining blocker:
  - execute `03-06` to land real depth-prepass mesh processing and Stage 3 publication proof before the base pass depends on those outputs

### 2026-04-15 — Phase 3 replanned into 15 smaller execute-ready steps after replacing the coarse plan set

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-01-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-02-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-03-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-04-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-05-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-06-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-07-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-08-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-10-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-11-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-12-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-13-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-14-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-15-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-RESEARCH.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure <each 03-0X-PLAN.md>`
  - repeated checker passes against the rebuilt Phase 03 plan set until no blockers remained
- Result:
  - the earlier coarse 5-plan Phase 03 set was discarded as too broad to execute safely
  - Phase 03 is now split into 15 smaller sequential plans covering:
    shared shader contracts, initial depth/base shader seeding, InitViews implementation and proof, depth-prepass implementation and proof, base-pass implementation and proof, velocity completion, GBuffer debug visualization, deferred-light shader family, Stage 12 CPU deferred lighting, automated proof sweep, and automated RenderDoc/ledger closeout
  - the final checker pass cleared blocking issues and warnings, and the plan set is now execution-ready
- Code / validation delta:
  - no implementation code changed in this replanning session
  - no build/tests/ShaderBake/RenderDoc execution evidence was collected yet because this session only rebuilt the planning surface
  - Phase 03 remains `not_started` from an implementation standpoint, but planning is complete and execution can start at `03-01`
- Remaining blocker:
  - none at the planning boundary; execute the Phase 03 plans in order starting with `03-01`

### 2026-04-14 — Phase 3 deferred-core planning artifacts created for the vortex workstream

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md` (local ignored workflow state restored)
  - `.planning/workstreams/vortex/STATE.md` (local ignored workflow state restored and advanced)
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-RESEARCH.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-01-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-02-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-03-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-04-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-05-PLAN.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init plan-phase 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" roadmap get-phase 3 --ws vortex --raw`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure <each 03-0X-PLAN.md>`
  - requirement-coverage grep over `SHDR-01`, `DEFR-01`, and `DEFR-02`
- Result:
  - the local `vortex` workstream planning surface was restored so GSD could route Phase 3 again
  - Phase 03 now has a research artifact, validation strategy, and five executable plan files aligned to the deferred-core dependency chain:
    shader foundation -> InitViews -> depth prepass -> base pass -> deferred lighting
  - structural validation passed for all five plan files, and the Phase 3 requirement IDs are fully covered by the plan set
  - proper plan-checker verification is still pending; execution must not start from this ledger entry alone
- Code / validation delta:
  - no implementation code changed
  - no build, tests, `ShaderBake`, or RenderDoc validation were run in this planning session because Phase 03 execution has not started yet
  - Phase 03 remains `not_started` from an implementation standpoint, and the plan-checker gate must pass before this planning set is treated as ready to execute
- Remaining blocker:
  - execute `.planning/workstreams/vortex/phases/03-deferred-core/03-01-PLAN.md` through `03-05-PLAN.md` and collect the proof pack defined in those plans

### 2026-04-14 — Phase 3 execute-phase preflight blocked at the planning boundary

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init execute-phase 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" workstream status vortex --raw`
  - `Get-ChildItem .planning/workstreams/vortex/phases`
  - `rg -n "Phase 3 planning|Phase 3 deferred-core planning|03-PLAN|03-VERIFICATION" design/vortex .planning/workstreams/vortex`
- Result:
  - `gsd-tools` resolves the `vortex` workstream, but Phase `3` still reports `phase_found: false`, `plan_count: 0`, and `Phase not found` in the phase index.
  - The workstream only contains planned phase directories `00`, `01`, and `02`; there is no `.planning/workstreams/vortex/phases/03-*` directory or `03-*-PLAN.md` set to execute.
  - The Vortex ledger and Phase 02 close-out artifacts both point to Phase 03 planning as the next valid step, so execute-phase must not claim Phase 03 work has started.
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run because execution stopped during preflight before a trustworthy Phase 03 task boundary existed
  - Phase 03 remains `not_started`; this session records a blocker only
- Remaining blocker:
  - create the Phase 03 `.planning/workstreams/vortex/phases/03-*` plan set, then rerun `/gsd-execute-phase 3 --ws vortex`

### 2026-04-14 — Phase 2 remediation landed; full Vortex proof is green, human review rerun still required

- Changed files this session:
  - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/PostRenderCleanup.cpp`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/RenderContext.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/*/.gitkeep`
  - `src/Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h`
  - `src/Oxygen/Vortex/Test/Fakes/Graphics.h`
  - `src/Oxygen/Vortex/Test/RenderContext_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneTextures_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp`
  - `design/vortex/lld/shader-contracts.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererPublication Oxygen.Vortex.SceneTextures Oxygen.Vortex.SceneRendererShell Oxygen.Vortex.RenderContext.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R '^Oxygen\.Vortex\.'`
  - `tools/cli/oxytidy.ps1 src/Oxygen/Vortex/Renderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp src/Oxygen/Vortex/Test/SceneTextures_test.cpp src/Oxygen/Vortex/Test/RenderContext_test.cpp -IncludeTests -Configuration Debug`
  - Phase-06 acceptance grep bundle
  - Phase-07 acceptance grep bundle
  - Phase-08 acceptance grep bundle
- Result:
  - `SceneTextures` is back to being a pure product-family owner while `SceneRenderer` owns setup progression, binding refresh, and extract state.
  - Scene-texture routing is descriptor-backed, `scene_color_uav` no longer aliases `scene_color_srv`, and Stage 21 / Stage 23 publish explicit artifact/history textures instead of live attachment aliases.
  - `RenderContext` now materializes every eligible frame view into `frame_views` with an explicit `active_view_index`, and the shell bootstrap extent uses the max scene-view envelope instead of first-view selection.
  - Publication tests are hack-free via `RendererPublicationProbe.h`, the required `SceneRenderer/Stages/` directory tree is committed, and the changed-file oxytidy sweep reports `0 warnings, 0 errors`.
  - The full `Oxygen.Vortex.*` suite passed (`28/28`).
- Code / validation delta:
  - Phase 2 implementation work is now materially present and proof-backed.
  - Automated verification is green at both the targeted-remediation and full-Vortex-suite level.
  - Phase 2 is still **not** ready to close because the human `PHASE2-REVIEW.md` approval gate has not yet been rerun on the remediated branch.
- Remaining blocker:
  - rerun the human Phase 2 review/approval gate (`02-05`) against the remediated tree and either approve close-out or capture any new human findings explicitly

### 2026-04-14 — Phase 2 closed after explicit human review pass

- Changed files this session:
  - `.planning/workstreams/vortex/phases/02-scenetextures-and-scenerenderer-shell/02-REVIEW.md`
  - `.planning/workstreams/vortex/phases/02-scenetextures-and-scenerenderer-shell/02-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/02-scenetextures-and-scenerenderer-shell/02-05-SUMMARY.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - human review verdict recorded as `PASS`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R '^Oxygen\.Vortex\.'`
  - `tools/cli/oxytidy.ps1 src/Oxygen/Vortex/Renderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp src/Oxygen/Vortex/Test/SceneTextures_test.cpp src/Oxygen/Vortex/Test/RenderContext_test.cpp -IncludeTests -Configuration Debug`
- Result:
  - the remediated Phase 02 tree was explicitly approved by human review
  - the full `Oxygen.Vortex.*` suite remains green (`28/28`)
  - the changed-file scoped `oxytidy` run remains clean (`0 warnings, 0 errors`)
  - Phase 02 is now closed and Phase 03 is the next valid work item
- Code / validation delta:
  - no new implementation code changed in this close-out step
  - the remaining non-automated approval gate is now satisfied
- Remaining blocker:
  - none for Phase 2; resume with Phase 3 planning

### 2026-04-13 — Phase 1 re-closed after 01-14 remediation and strengthened proof

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/RendererCapability.h`
  - `src/Oxygen/Vortex/Internal/CompositingPass.h`
  - `src/Oxygen/Vortex/Internal/CompositingPass.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ShaderDebugMode.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ShaderPassConfig.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ToneMapPassConfig.h`
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Link_test.cpp`
  - `src/Oxygen/Vortex/Test/RendererCapability_test.cpp`
  - `src/Oxygen/Vortex/Test/RendererCompositionQueue_test.cpp`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n 'kDeferredShading' src/Oxygen/Vortex/RendererCapability.h`
  - `rg -n 'Internal/RenderContextPool.h' src/Oxygen/Vortex/Renderer.h`
  - `rg -n 'SceneRenderer/ShaderDebugMode.h|SceneRenderer/ShaderPassConfig.h|SceneRenderer/ToneMapPassConfig.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'oxygen::imgui' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.' --output-on-failure`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
- Result:
  - `Renderer::OnCompositing()` now drains queued submissions through a Vortex-owned compositing path instead of clearing them unused
  - `RendererCapabilityFamily` now contains the required Phase 1 `kDeferredShading` vocabulary entry, while the default runtime capability set is reduced to truthful substrate families only
  - the temporary `FramePlanBuilder` support contracts now live under `SceneRenderer/Internal/` and are no longer exported as public Phase 1 API
  - `oxygen-vortex` no longer links `oxygen::imgui`, and the final Ninja query shows no `Oxygen.ImGui` edge
  - `Renderer.h` no longer leaks `Internal/RenderContextPool.h`
  - Vortex now carries local regression coverage for composition-queue execution and capability/default hygiene, and the hardened `Link_test` also rejects the reopened boundary regressions
  - the strengthened Vortex-only proof suite passed on the repaired tree: Vortex build, Vortex-local tests (`3/3`), hermeticity scan, and final target-edge proof
- Code / validation delta:
  - Phase 1 returns from `reopened` to `done`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md` is restored from `gaps_found` to `passed`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md` marks the `01-14` remediation checks green
  - Phase 2 may resume from the repaired Phase 1 baseline
- Remaining blocker:
  - none at the Phase 1 boundary; next action is Phase 2 execution

### 2026-04-13 — Phase 1 reopened after comprehensive architecture/LLD compliance review

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-14-PLAN.md`
- Commands used for verification:
  - `rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Renderer.LinkTest --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.LinkTest$' --output-on-failure`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\.Renderer\.(LinkTest|CompositionPlanner\.Tests|SceneCameraViewResolver\.Tests|RenderContext\.Tests|RenderContextMaterializer\.Tests|RendererCapability\.Tests|RendererCompositionQueue\.Tests|RendererPublicationSplit\.Tests|RendererFacadePresets\.Tests|SinglePassHarnessFacade\.Tests|RenderGraphHarnessFacade\.Tests|OffscreenSceneFacade\.Tests|GpuTimelineProfiler\.Tests|LightCullingConfig\.Tests|ScenePrep\.Tests|UploadCoordinator\.Tests|RingBufferStaging\.Tests|UploadTracker\.Tests|AtlasBuffer\.Tests|UploadPlanner\.Tests|TransientStructuredBuffer\.Tests|TextureBinder\.Tests|MaterialBinder\.Tests|TransformUploader\.Tests|DrawMetadataEmitter\.Tests)'`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
  - `rg -n 'DispatchSceneRendererPreRender|DispatchSceneRendererRender|DispatchSceneRendererCompositing|RegisterComposition|pending_compositions_' src/Oxygen/Vortex/Renderer.cpp src/Oxygen/Vortex/Renderer.h`
  - `rg -n 'kDeferredShading|RendererCapabilityFamily' src/Oxygen/Vortex/RendererCapability.h design/vortex/DESIGN.md design/vortex/lld/substrate-migration-guide.md`
  - `rg -n 'oxygen::imgui|RenderContextPool.h|ShaderDebugMode|ShaderPassConfig|ToneMapPassConfig' src/Oxygen/Vortex/CMakeLists.txt src/Oxygen/Vortex/Renderer.h src/Oxygen/Vortex/SceneRenderer`
- Result:
  - the earlier `01-13` hermeticity proof remains valid: the Vortex source tree has no `Oxygen/Renderer/*` include seam, `oxygen-vortex` builds, `Oxygen.Vortex.LinkTest` passes, the targeted 25-test legacy substrate regression suite passes, and the Debug Ninja query still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
  - however, the broader compliance review found five unresolved Phase 1 violations:
    - `Renderer::OnCompositing()` still drops queued composition work instead of preserving the renderer-core composition execution contract
    - `RendererCapabilityFamily` is still missing the required Phase 1 `kDeferredShading` vocabulary entry, and the default runtime capability set over-claims later-domain families
    - `ShaderDebugMode`, `ShaderPassConfig`, and `ToneMapPassConfig` are exported as public SceneRenderer-facing contracts even though Phase 1 only authorized the bounded step-1.7 redistribution slice
    - `oxygen-vortex` still publicly links `oxygen::imgui`, leaking Diagnostics/UI domain ownership into the Phase 1 substrate boundary
    - public `Renderer.h` still includes `Oxygen/Vortex/Internal/RenderContextPool.h`, violating the `Internal/` header rule
  - the previous `Phase 1 is complete` claim is therefore superseded
- Code / validation delta:
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md` is downgraded from `passed` to `gaps_found`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md` now defines the remediation verification pack required to close the reopened gaps
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-14-PLAN.md` is now the active repair plan for Phase 1
  - no Phase 2 implementation work may begin while these reopened Phase 1 issues remain
- Remaining blocker:
  - execute `01-14` to repair the renderer-core contract and ABI/dependency hygiene gaps, then rerun the strengthened Phase 1 proof suite before restoring any completion claim

### 2026-04-13 — Phase 1 plan 01-13 closed FOUND-03 with a Vortex-local hermeticity guard and final proof refresh [historical entry later superseded by the reopened-phase review]

- Changed files this session:
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Link_test.cpp`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.LinkTest$' --output-on-failure`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\.Renderer\.(LinkTest|CompositionPlanner\.Tests|SceneCameraViewResolver\.Tests|RenderContext\.Tests|RenderContextMaterializer\.Tests|RendererCapability\.Tests|RendererCompositionQueue\.Tests|RendererPublicationSplit\.Tests|RendererFacadePresets\.Tests|SinglePassHarnessFacade\.Tests|RenderGraphHarnessFacade\.Tests|OffscreenSceneFacade\.Tests|GpuTimelineProfiler\.Tests|LightCullingConfig\.Tests|ScenePrep\.Tests|UploadCoordinator\.Tests|RingBufferStaging\.Tests|UploadTracker\.Tests|AtlasBuffer\.Tests|UploadPlanner\.Tests|TransientStructuredBuffer\.Tests|TextureBinder\.Tests|MaterialBinder\.Tests|TransformUploader\.Tests|DrawMetadataEmitter\.Tests)'`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
- Result:
  - `Oxygen.Vortex.LinkTest` now scans `src/Oxygen/Vortex` recursively before executing the smoke path, so the Vortex-local test surface fails immediately if any legacy renderer include seam returns
  - the Vortex-wide seam scan still reports no `Oxygen/Renderer/*` match after the `01-12` `FramePlanBuilder` repair
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4` passed again with the guard compiled into the test surface
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.LinkTest$' --output-on-failure` passed with the hermeticity guard active
  - the targeted legacy substrate regression suite still passed (`25/25`)
  - the final Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` still showed no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
  - `01-VERIFICATION.md` now moves from `gaps_found` to `passed`, and `FOUND-03` is satisfied
- Code / validation delta:
  - Phase 1 now has both the source-level seam fix and a local regression guard against reintroducing `Oxygen/Renderer/*` under `src/Oxygen/Vortex`
  - the build, smoke path, targeted regressions, and linked-artifact dependency proof are all refreshed after the guard landed
  - Phase 1 was reported as **complete** at this point, but that claim is now superseded by the later reopened-phase review above
- Remaining blocker:
  - historical state only; see the reopened-phase blocker entry above for the current control point

### 2026-04-13 — Phase 1 plan 01-12 removed the last FramePlanBuilder source seam

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h`
  - `src/Oxygen/Vortex/SceneRenderer/ShaderDebugMode.h`
  - `src/Oxygen/Vortex/SceneRenderer/ShaderPassConfig.h`
  - `src/Oxygen/Vortex/SceneRenderer/ToneMapPassConfig.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `powershell -NoProfile -Command "$bad = rg -n '#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp; if ($LASTEXITCODE -ne 1) { Write-Error 'legacy renderer include seam remains in FramePlanBuilder'; exit 1 }; rg -n 'SceneRenderer/ShaderDebugMode.h|SceneRenderer/ShaderPassConfig.h|SceneRenderer/ToneMapPassConfig.h' src/Oxygen/Vortex/CMakeLists.txt | Out-Null"`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4`
  - `powershell -NoProfile -Command "$bad = rg -n '#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex; if ($LASTEXITCODE -ne 1) { Write-Error 'legacy renderer include seam still exists under src/Oxygen/Vortex'; exit 1 }; exit 0"`
- Result:
  - `FramePlanBuilder` now consumes Vortex-owned `SceneRenderer/ShaderDebugMode.h`, `ShaderPassConfig.h`, and `ToneMapPassConfig.h` instead of importing `Oxygen/Renderer/*`
  - the Vortex module now exports the rehomed planning contracts from `src/Oxygen/Vortex/SceneRenderer/`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4` passed after the seam fix
  - the Vortex-wide include scan now returns no `Oxygen/Renderer/*` matches under `src/Oxygen/Vortex`
  - the stale `01-11` Phase 1 completion claim is superseded by the explicit `01-VERIFICATION.md` gap report; Phase 1 remains `in_progress` until `01-13` adds the hermeticity guard and reruns the full proof suite
- Code / validation delta:
  - the remaining `FOUND-03` source/API seam in `FramePlanBuilder` is now **removed**
  - the Vortex build and smoke target evidence is refreshed after the seam fix
  - Phase 1 remains `in_progress`; `01-13` still owns the Vortex-side hermeticity guard and the final re-verification update
- Remaining blocker:
  - execute `01-13` to add the Vortex-local hermeticity guard, rerun the full proof suite, and update `01-VERIFICATION.md` before claiming Phase 1 complete

### 2026-04-13 — Phase 1 plan 01-11 closed step 1.9 and recorded the smoke/regression evidence

- Changed files this session:
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Fakes/Graphics.h`
  - `src/Oxygen/Vortex/Test/Link_test.cpp`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `powershell -NoProfile -Command "cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.LinkTest$' --output-on-failure"`
  - `powershell -NoProfile -Command "cmake --build --preset windows-debug --parallel 4"`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\\.Renderer\\.(LinkTest|CompositionPlanner\\.Tests|SceneCameraViewResolver\\.Tests|RenderContext\\.Tests|RenderContextMaterializer\\.Tests|RendererCapability\\.Tests|RendererCompositionQueue\\.Tests|RendererPublicationSplit\\.Tests|RendererFacadePresets\\.Tests|SinglePassHarnessFacade\\.Tests|RenderGraphHarnessFacade\\.Tests|OffscreenSceneFacade\\.Tests|GpuTimelineProfiler\\.Tests|LightCullingConfig\\.Tests|ScenePrep\\.Tests|UploadCoordinator\\.Tests|RingBufferStaging\\.Tests|UploadTracker\\.Tests|AtlasBuffer\\.Tests|UploadPlanner\\.Tests|TransientStructuredBuffer\\.Tests|TextureBinder\\.Tests|MaterialBinder\\.Tests|TransformUploader\\.Tests|DrawMetadataEmitter\\.Tests)'`
- Result:
  - `Oxygen.Vortex.LinkTest` now uses a Vortex-local backend-free fake graphics harness, constructs `oxygen::vortex::Renderer` with an empty capability set, and drives `OnFrameStart`, `OnTransformPropagation`, `OnPreRender`, `OnRender`, `OnCompositing`, and `OnFrameEnd`
  - the Vortex smoke path passed through `ctest`, proving the stripped renderer frame hooks execute without relying on `Oxygen.Renderer`
  - the targeted legacy substrate regression suite passed after the post-orchestrator `FOUND-03` dependency-edge proof from `01-10`
  - the generated workspace does not define `ctest --preset windows-debug`, so the regression gate was run with the equivalent Debug build-tree invocation `ctest --test-dir out/build-ninja -C Debug ...`
- Code / validation delta:
  - step `1.9` is now **complete**
  - `01-11` landed the smoke/regression evidence, but the source-level `FOUND-03` seam in `FramePlanBuilder` remained unresolved until the later `01-12` repair identified by `01-VERIFICATION.md`
  - Phase 1 remained `in_progress` pending the seam fix and the later `01-13` hermeticity guard plus re-verification
- Remaining blocker:
  - execute `01-12` to remove the `FramePlanBuilder` seam, then `01-13` to add the hermeticity guard and rerun the full proof suite

### 2026-04-13 — Phase 1 plan 01-10 landed step 1.6, the stripped renderer orchestrator, and the final FOUND-03 proof

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Errors.h`
  - `src/Oxygen/Vortex/FacadePresets.h`
  - `src/Oxygen/Vortex/Internal/ViewLifecycleService.h`
  - `src/Oxygen/Vortex/Passes/ComputeRenderPass.cpp`
  - `src/Oxygen/Vortex/Passes/ComputeRenderPass.h`
  - `src/Oxygen/Vortex/Passes/GraphicsRenderPass.cpp`
  - `src/Oxygen/Vortex/Passes/GraphicsRenderPass.h`
  - `src/Oxygen/Vortex/Passes/RenderPass.cpp`
  - `src/Oxygen/Vortex/Passes/RenderPass.h`
  - `src/Oxygen/Vortex/RenderContext.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/SceneCameraViewResolver.cpp`
  - `src/Oxygen/Vortex/SceneCameraViewResolver.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_|LightManager|ShadowManager|EnvironmentLightingService' src/Oxygen/Vortex/Errors.h src/Oxygen/Vortex/FacadePresets.h src/Oxygen/Vortex/Passes/ComputeRenderPass.cpp src/Oxygen/Vortex/Passes/ComputeRenderPass.h src/Oxygen/Vortex/Passes/GraphicsRenderPass.cpp src/Oxygen/Vortex/Passes/GraphicsRenderPass.h src/Oxygen/Vortex/Passes/RenderPass.cpp src/Oxygen/Vortex/Passes/RenderPass.h src/Oxygen/Vortex/RenderContext.h src/Oxygen/Vortex/SceneCameraViewResolver.cpp src/Oxygen/Vortex/SceneCameraViewResolver.h`
  - `rg -n 'ForwardPipeline|RenderingPipeline|LightManager|ShadowManager|EnvironmentLightingService|Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Renderer.h src/Oxygen/Vortex/Renderer.cpp`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
- Result:
  - the remaining Phase 1 root-support files now live under Vortex ownership as `Errors.h`, `FacadePresets.h`, `RenderContext.h`, and `SceneCameraViewResolver.cpp/.h`
  - the step-`1.6` pass bases now live under `src/Oxygen/Vortex/Passes/` and compile against Vortex-owned `RenderContext` / `Renderer` contracts instead of the legacy renderer namespace
  - `Renderer.h/.cpp` now provide a stripped substrate-only Vortex renderer shell with no `ForwardPipeline`, `RenderingPipeline`, `LightManager`, `ShadowManager`, or `EnvironmentLightingService` ownership
  - `oxygen-vortex` builds successfully in Debug after the stripped renderer orchestrator lands
  - the final Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` lists only Vortex objects plus non-renderer engine dependencies; it contains no `oxygen-renderer` / `Oxygen.Renderer` dependency edge after the orchestrator landed
- Code / validation delta:
  - steps `1.6` and `1.8` are now **complete**
  - `FOUND-03` now has the final post-orchestrator dependency-edge proof required by `01-10`
  - Phase 1 remains `in_progress` because step `1.9` smoke plus the legacy substrate regression gate are still owned by `01-11`
- Remaining blocker:
  - execute `01-11` to run the Vortex smoke path and the targeted legacy substrate regressions before claiming Phase 1 complete

### 2026-04-13 — Phase 1 plan 01-09 closed the private half of step 1.7

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Internal/CompositionPlanner.cpp`
  - `src/Oxygen/Vortex/Internal/CompositionPlanner.h`
  - `src/Oxygen/Vortex/Internal/CompositionViewImpl.cpp`
  - `src/Oxygen/Vortex/Internal/CompositionViewImpl.h`
  - `src/Oxygen/Vortex/Internal/FrameViewPacket.cpp`
  - `src/Oxygen/Vortex/Internal/FrameViewPacket.h`
  - `src/Oxygen/Vortex/Internal/ViewLifecycleService.cpp`
  - `src/Oxygen/Vortex/Internal/ViewLifecycleService.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ViewRenderPlan.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Renderer/Pipeline|PipelineSettings|Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Internal/CompositionPlanner.cpp src/Oxygen/Vortex/Internal/CompositionPlanner.h src/Oxygen/Vortex/Internal/CompositionViewImpl.cpp src/Oxygen/Vortex/Internal/CompositionViewImpl.h src/Oxygen/Vortex/Internal/FrameViewPacket.cpp src/Oxygen/Vortex/Internal/FrameViewPacket.h src/Oxygen/Vortex/Internal/ViewLifecycleService.cpp src/Oxygen/Vortex/Internal/ViewLifecycleService.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n '\| 1\.7 \||FramePlanBuilder.cpp|ViewRenderPlan.h|01-10' design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the renderer-core private step-`1.7` files now live under `src/Oxygen/Vortex/Internal/`
  - `FramePlanBuilder.cpp/.h` and `ViewRenderPlan.h` now live under `src/Oxygen/Vortex/SceneRenderer/Internal/`, matching `PROJECT-LAYOUT.md`
  - `FramePlanBuilder` no longer owns the discarded `PipelineSettings` type; it now consumes a local planning input shape scoped to the scene-renderer private slice
  - `oxygen-vortex` builds successfully in Debug after the full private step-`1.7` rehome lands
- Code / validation delta:
  - step `1.7` is now **complete**
  - step `1.6` remains deferred to `01-10`; this plan did not pull the pass bases, `RenderContext.h`, or `Renderer.h/.cpp` forward
  - no Vortex runtime/facade validation was run in this plan
- Remaining blocker:
  - execute repaired `01-10` to land step `1.6`, the remaining root-support files, the stripped renderer orchestrator, and the final post-orchestrator dependency-edge proof

### 2026-04-13 — Phase 1 plan 01-08 landed the public step-1.7 header slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/CompositionView.h`
  - `src/Oxygen/Vortex/RendererCapability.h`
  - `src/Oxygen/Vortex/RenderMode.h`
  - `src/Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'RenderingPipeline|PipelineFeature|PipelineSettings|Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/CompositionView.h src/Oxygen/Vortex/RendererCapability.h src/Oxygen/Vortex/RenderMode.h`
  - `rg -n 'CompositionView.h|RendererCapability.h|RenderMode.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n '\| 1\.6 \||\| 1\.7 \||01-09|01-10' design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the public step-`1.7` root vocabulary now lives under `src/Oxygen/Vortex/` as `CompositionView.h`, `RendererCapability.h`, and `RenderMode.h`
  - `DepthPrePassPolicy.h` now lives under `src/Oxygen/Vortex/SceneRenderer/`, matching `PROJECT-LAYOUT.md`
  - `src/Oxygen/Vortex/CMakeLists.txt` lists only the public step-`1.7` header slice added by `01-08`
  - `oxygen-vortex` builds successfully in Debug after the public header slice lands
- Code / validation delta:
  - step `1.7` is now **in progress**: the public header half is complete, but the private composition infrastructure remains deferred to `01-09`
  - step `1.6` remains deferred to `01-10`; this plan did not pull the pass bases, `RenderContext.h`, or `Renderer.h/.cpp` forward
  - no Vortex runtime/facade validation was run in this plan
- Remaining blocker:
  - execute repaired `01-09` to land the private half of step `1.7`, then `01-10` to land step `1.6` with the later root-contract wave

### 2026-04-13 — Phase 1 plan 01-07 migrated the ScenePrep execution slice and selected substrate-only internals

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Internal/BlueNoiseData.h`
  - `src/Oxygen/Vortex/Internal/PerViewStructuredPublisher.h`
  - `src/Oxygen/Vortex/Internal/RenderContextMaterializer.h`
  - `src/Oxygen/Vortex/Internal/RenderContextPool.h`
  - `src/Oxygen/Vortex/Internal/RenderScope.h`
  - `src/Oxygen/Vortex/Internal/ViewConstantsManager.cpp`
  - `src/Oxygen/Vortex/Internal/ViewConstantsManager.h`
  - `src/Oxygen/Vortex/ScenePrep/Extractors.h`
  - `src/Oxygen/Vortex/ScenePrep/Finalizers.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepState.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/ScenePrep/Extractors.h src/Oxygen/Vortex/ScenePrep/Finalizers.h src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h`
  - `rg -n 'ScenePrep/ScenePrepPipeline.cpp|ScenePrep/Extractors.h|ScenePrep/Finalizers.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_|IIblProvider|ISkyCaptureProvider|ISkyAtmosphereLutProvider|IBrdfLutProvider|IblManager|SunResolver|EnvironmentStaticDataManager|ConventionalShadow' src/Oxygen/Vortex/Internal/BlueNoiseData.h src/Oxygen/Vortex/Internal/PerViewStructuredPublisher.h src/Oxygen/Vortex/Internal/RenderContextMaterializer.h src/Oxygen/Vortex/Internal/RenderContextPool.h src/Oxygen/Vortex/Internal/RenderScope.h src/Oxygen/Vortex/Internal/ViewConstantsManager.cpp src/Oxygen/Vortex/Internal/ViewConstantsManager.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll`
- Result:
  - `src/Oxygen/Vortex/ScenePrep/Extractors.h`, `Finalizers.h`, and `ScenePrepPipeline.cpp/.h` now live under Vortex ownership, so the deferred execution half of step `1.4` is no longer outstanding
  - the selected substrate-only utility slice now lives under `src/Oxygen/Vortex/Internal/` without importing light, shadow, or environment-specific internals
  - `RenderContextPool.h` and `RenderContextMaterializer.h` were moved into Vortex ownership as dependency-safe templates so `01-07` did not have to pull `RenderContext.h` or `Renderer.h` forward from `01-10`
  - the stale `LightManager` hook was removed from the Vortex `ScenePrep` state/pipeline because compiling the new execution slice exposed it as a later-domain dependency outside the Phase 1 substrate boundary
  - `oxygen-vortex` builds successfully in Debug after the ScenePrep execution and selected internal utility slice land
  - the generated Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - steps `1.4` and `1.5` are now complete with build and dependency-edge evidence
  - no Vortex runtime/facade validation was run in this plan
- Remaining blocker:
  - execute repaired `01-08` to land only the public half of step `1.7`, then `01-09`, then `01-10` to land step `1.6` with the later root-contract wave

### 2026-04-13 — Phase 1 tail repaired after the `01-08` pass-base/root-contract blocker

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-11-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-11-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/STATE.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `Get-Content design/vortex/PLAN.md`
  - `Get-Content design/vortex/PROJECT-LAYOUT.md`
  - `Get-Content src/Oxygen/Renderer/Passes/RenderPass.cpp`
  - `Get-Content src/Oxygen/Renderer/Passes/GraphicsRenderPass.cpp`
  - `Get-Content src/Oxygen/Renderer/Passes/ComputeRenderPass.cpp`
- Result:
  - `01-08` now owns only `CompositionView.h`, `RendererCapability.h`, `RenderMode.h`, and `SceneRenderer/DepthPrePassPolicy.h`
  - the plan set no longer claims the step-`1.6` pass bases can land before Vortex owns `RenderContext` / `Renderer`
  - `01-09` still closes the private half of step `1.7`
  - `01-10` now owns the step-`1.6` pass bases together with the later-wave root contracts, stripped renderer orchestrator, and final post-orchestrator dependency-edge proof
  - the roadmap, validation contract, resume note, and state file now all point execution back to the completed-state handoff at `01-08`
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during this planning repair
  - Phase 1 remains `in_progress`; the next executable plan is repaired `01-08`
- Remaining blocker:
  - execute repaired `01-08`, `01-09`, and `01-10` in order from the current completed state at `01-07`

### 2026-04-13 — Phase 1 plan 01-06 migrated the remaining ScenePrep-only data/config slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/ScenePrep/CollectionConfig.h`
  - `src/Oxygen/Vortex/ScenePrep/Concepts.h`
  - `src/Oxygen/Vortex/ScenePrep/FinalizationConfig.h`
  - `src/Oxygen/Vortex/ScenePrep/RenderItemProto.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepContext.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepState.h`
  - `src/Oxygen/Vortex/ScenePrep/Types.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/ScenePrep/CollectionConfig.h src/Oxygen/Vortex/ScenePrep/Concepts.h src/Oxygen/Vortex/ScenePrep/FinalizationConfig.h src/Oxygen/Vortex/ScenePrep/RenderItemProto.h src/Oxygen/Vortex/ScenePrep/ScenePrepContext.h src/Oxygen/Vortex/ScenePrep/ScenePrepState.h src/Oxygen/Vortex/ScenePrep/Types.h`
  - `rg -n 'ScenePrep/CollectionConfig.h|ScenePrep/ScenePrepContext.h|ScenePrep/Types.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll`
- Result:
  - the remaining ScenePrep-only data/config contracts now live under `src/Oxygen/Vortex/ScenePrep/`
  - `src/Oxygen/Vortex/CMakeLists.txt` now exports those headers without re-owning the ABI files already landed by `01-04`
  - `CollectionConfig.h` and `FinalizationConfig.h` keep the deferred execution entry points as forward declarations instead of widening `01-06` into `Extractors.h`, `Finalizers.h`, or `ScenePrepPipeline.*`
  - `oxygen-vortex` builds successfully in Debug after the ScenePrep-only header slice lands
  - the generated Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.4` is now **in progress**: the ScenePrep-only data/config slice is complete, but `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h` remain deferred to `01-07`
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute repaired `01-07` to land `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h`, then close step `1.4`

### 2026-04-13 — Phase 1 plan 01-05 migrated `Resources/*` and closed step 1.3

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp`
  - `src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h`
  - `src/Oxygen/Vortex/Resources/GeometryUploader.cpp`
  - `src/Oxygen/Vortex/Resources/GeometryUploader.h`
  - `src/Oxygen/Vortex/Resources/IResourceBinder.h`
  - `src/Oxygen/Vortex/Resources/MaterialBinder.cpp`
  - `src/Oxygen/Vortex/Resources/MaterialBinder.h`
  - `src/Oxygen/Vortex/Resources/TextureBinder.cpp`
  - `src/Oxygen/Vortex/Resources/TextureBinder.h`
  - `src/Oxygen/Vortex/Resources/TransformUploader.cpp`
  - `src/Oxygen/Vortex/Resources/TransformUploader.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h src/Oxygen/Vortex/Resources/GeometryUploader.cpp src/Oxygen/Vortex/Resources/GeometryUploader.h src/Oxygen/Vortex/Resources/IResourceBinder.h src/Oxygen/Vortex/Resources/MaterialBinder.cpp src/Oxygen/Vortex/Resources/MaterialBinder.h src/Oxygen/Vortex/Resources/TextureBinder.cpp src/Oxygen/Vortex/Resources/TextureBinder.h src/Oxygen/Vortex/Resources/TransformUploader.cpp src/Oxygen/Vortex/Resources/TransformUploader.h`
  - `rg -n 'ScenePrepState.h|ScenePrep/Types.h' src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `powershell -NoProfile -Command \"$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; $query = & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll 2>&1; if ($query -match 'oxygen-renderer|Oxygen\\.Renderer') { Write-Error 'oxygen-vortex target still depends on Oxygen.Renderer'; exit 1 }\"`
- Result:
  - the full step-`1.3` resource subsystem now lives under `src/Oxygen/Vortex/Resources/`
  - `src/Oxygen/Vortex/CMakeLists.txt` now wires the migrated resource files into `oxygen-vortex`
  - the stale `ScenePrepState.h` and `ScenePrep/Types.h` includes were removed instead of widening the repaired prerequisite boundary
  - `oxygen-vortex` builds successfully in Debug after the resource slice lands
  - the generated Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.3` is now **complete**
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute repaired `01-06` to migrate the remaining ScenePrep-only data/config files

### 2026-04-13 — Phase 1 plan 01-04 landed the prerequisite ABI bundle for resources

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/PreparedSceneFrame.cpp`
  - `src/Oxygen/Vortex/PreparedSceneFrame.h`
  - `src/Oxygen/Vortex/ScenePrep/GeometryRef.h`
  - `src/Oxygen/Vortex/ScenePrep/Handles.h`
  - `src/Oxygen/Vortex/ScenePrep/MaterialRef.h`
  - `src/Oxygen/Vortex/ScenePrep/RenderItemData.h`
  - `src/Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h`
  - `src/Oxygen/Vortex/Types/DrawMetadata.h`
  - `src/Oxygen/Vortex/Types/MaterialShadingConstants.h`
  - `src/Oxygen/Vortex/Types/PassMask.h`
  - `src/Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h src/Oxygen/Vortex/Types/DrawMetadata.h src/Oxygen/Vortex/Types/MaterialShadingConstants.h src/Oxygen/Vortex/Types/PassMask.h src/Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/PreparedSceneFrame.cpp src/Oxygen/Vortex/PreparedSceneFrame.h src/Oxygen/Vortex/ScenePrep/GeometryRef.h src/Oxygen/Vortex/ScenePrep/Handles.h src/Oxygen/Vortex/ScenePrep/MaterialRef.h src/Oxygen/Vortex/ScenePrep/RenderItemData.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n 'PreparedSceneFrame.cpp|ScenePrep/Handles.h|ScenePrep/RenderItemData.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'prerequisite ABI|01-05|step 1\\.3' design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the full prerequisite ABI bundle required by `Resources/*` now lives under
    `src/Oxygen/Vortex/`: `PreparedSceneFrame.h/.cpp`,
    `ScenePrep/GeometryRef.h`, `ScenePrep/Handles.h`,
    `ScenePrep/MaterialRef.h`, `ScenePrep/RenderItemData.h`,
    `Types/PassMask.h`, `Types/DrawMetadata.h`,
    `Types/MaterialShadingConstants.h`,
    `Types/ProceduralGridMaterialConstants.h`, and
    `Types/ConventionalShadowDrawRecord.h`
  - `src/Oxygen/Vortex/CMakeLists.txt` now wires the complete prerequisite
    ABI bundle into `oxygen-vortex` while still leaving `Resources/*` out of
    the target for repaired `01-05`
  - `oxygen-vortex` builds successfully in Debug with the prerequisite bundle
    alone
  - the Vortex target emits an IDE warning listing `Resources/*` files that
    exist on disk but are not yet part of the target; this is expected at the
    repaired `01-04` boundary and confirms those files remain deferred to
    `01-05`
- Code / validation delta:
  - repaired `01-04` is now **complete**
  - step `1.3` remains open because no `Resources/*` implementation files were
    added to the Vortex target in this plan
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute repaired `01-05` to migrate `Resources/*` on top of the landed
    prerequisite ABI bundle

### 2026-04-13 — Phase 1 plan repair resolved the `01-04` prerequisite blocker

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-07-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-07-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-RESEARCH.md`
  - `Get-Content design/vortex/PLAN.md`
  - `Get-Content design/vortex/PROJECT-LAYOUT.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md"`
- Result:
  - `01-04` now truthfully owns only the prerequisite ABI bundle that `Resources/*` already depends on:
    `Types/PassMask.h`, `Types/DrawMetadata.h`,
    `Types/MaterialShadingConstants.h`,
    `Types/ProceduralGridMaterialConstants.h`,
    `Types/ConventionalShadowDrawRecord.h`,
    `ScenePrep/Handles.h`, `ScenePrep/GeometryRef.h`,
    `ScenePrep/MaterialRef.h`, `ScenePrep/RenderItemData.h`, and
    `PreparedSceneFrame.h/.cpp`
  - `01-05` now owns `Resources/*` and closes step `1.3`
  - the remaining ScenePrep, pass-base, composition, and orchestrator scopes
    shift down one slot so no single remaining plan exceeds the checker
    file-budget threshold
  - `01-10` now owns the step-`1.6` pass bases together with the remaining
    root-support files plus the stripped orchestrator and records the final
    post-orchestrator dependency-edge proof
  - the roadmap, validation contract, and resume note now all point execution
    back to the repaired `01-04` boundary
  - task-structure verification confirms all three repaired plans still have
    two executable tasks with `files`, `action`, `verify`, `done`,
    `<read_first>`, and `<acceptance_criteria>` intact
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during this planning repair
  - Phase 1 remains `in_progress`; execution must resume at `01-04` and still
    collect build/test evidence before any stronger completion claim
- Remaining blocker:
  - execute the repaired `01-04` plan and verify the prerequisite ABI bundle
    in code before moving to repaired `01-05` for the actual resource
    migration

### 2026-04-13 — Phase 1 plan 01-03 completed step-1.2 upload migration

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/RendererTag.h`
  - `src/Oxygen/Vortex/Upload/AtlasBuffer.cpp`
  - `src/Oxygen/Vortex/Upload/AtlasBuffer.h`
  - `src/Oxygen/Vortex/Upload/Errors.cpp`
  - `src/Oxygen/Vortex/Upload/Errors.h`
  - `src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.cpp`
  - `src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.h`
  - `src/Oxygen/Vortex/Upload/RingBufferStaging.cpp`
  - `src/Oxygen/Vortex/Upload/RingBufferStaging.h`
  - `src/Oxygen/Vortex/Upload/StagingProvider.cpp`
  - `src/Oxygen/Vortex/Upload/StagingProvider.h`
  - `src/Oxygen/Vortex/Upload/TransientStructuredBuffer.cpp`
  - `src/Oxygen/Vortex/Upload/TransientStructuredBuffer.h`
  - `src/Oxygen/Vortex/Upload/Types.h`
  - `src/Oxygen/Vortex/Upload/UploaderTag.h`
  - `src/Oxygen/Vortex/Upload/UploadCoordinator.cpp`
  - `src/Oxygen/Vortex/Upload/UploadCoordinator.h`
  - `src/Oxygen/Vortex/Upload/UploadHelpers.cpp`
  - `src/Oxygen/Vortex/Upload/UploadHelpers.h`
  - `src/Oxygen/Vortex/Upload/UploadPlanner.cpp`
  - `src/Oxygen/Vortex/Upload/UploadPlanner.h`
  - `src/Oxygen/Vortex/Upload/UploadPolicy.cpp`
  - `src/Oxygen/Vortex/Upload/UploadPolicy.h`
  - `src/Oxygen/Vortex/Upload/UploadTracker.cpp`
  - `src/Oxygen/Vortex/Upload/UploadTracker.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Upload/AtlasBuffer.cpp src/Oxygen/Vortex/Upload/AtlasBuffer.h src/Oxygen/Vortex/Upload/Errors.cpp src/Oxygen/Vortex/Upload/Errors.h src/Oxygen/Vortex/Upload/RingBufferStaging.cpp src/Oxygen/Vortex/Upload/RingBufferStaging.h src/Oxygen/Vortex/Upload/StagingProvider.cpp src/Oxygen/Vortex/Upload/StagingProvider.h src/Oxygen/Vortex/Upload/TransientStructuredBuffer.cpp src/Oxygen/Vortex/Upload/TransientStructuredBuffer.h src/Oxygen/Vortex/Upload/Types.h src/Oxygen/Vortex/Upload/UploaderTag.h`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.cpp src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.h src/Oxygen/Vortex/Upload/UploadCoordinator.cpp src/Oxygen/Vortex/Upload/UploadCoordinator.h src/Oxygen/Vortex/Upload/UploadHelpers.cpp src/Oxygen/Vortex/Upload/UploadHelpers.h src/Oxygen/Vortex/Upload/UploadPlanner.cpp src/Oxygen/Vortex/Upload/UploadPlanner.h src/Oxygen/Vortex/Upload/UploadPolicy.cpp src/Oxygen/Vortex/Upload/UploadPolicy.h src/Oxygen/Vortex/Upload/UploadTracker.cpp src/Oxygen/Vortex/Upload/UploadTracker.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll`
- Result:
  - the complete step-`1.2` upload slice now lives under
    `src/Oxygen/Vortex/Upload/`, including the foundation, staging,
    coordination, planner, policy, helper, and tracker files
  - `src/Oxygen/Vortex/RendererTag.h` was added so the migrated upload headers
    can stop including `Oxygen/Renderer/RendererTag.h` while keeping the
    migration mechanical
  - `oxygen-vortex` builds successfully in Debug after the full upload
    migration lands
  - the generated Debug Ninja target query for
    `bin/Debug/Oxygen.Vortex-d.dll` shows no `oxygen-renderer` /
    `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.2` is now **complete**
  - no Vortex runtime or link-test validation was run in this plan
- Remaining blocker:
  - execute `01-04` to migrate the step-`1.3` resources slice

### 2026-04-13 — Phase 1 plan 01-02 completed the remaining step-1.1 type migration

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Types/EnvironmentViewData.h`
  - `src/Oxygen/Vortex/Types/ViewColorData.h`
  - `src/Oxygen/Vortex/Types/ViewConstants.cpp`
  - `src/Oxygen/Vortex/Types/ViewConstants.h`
  - `src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/EnvironmentViewData.h src/Oxygen/Vortex/Types/LightCullingConfig.h src/Oxygen/Vortex/Types/SyntheticSunData.h src/Oxygen/Vortex/Types/ViewColorData.h src/Oxygen/Vortex/Types/ViewConstants.cpp src/Oxygen/Vortex/Types/ViewConstants.h src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query oxygen-vortex`
- Result:
  - the remaining step-`1.1` files now live under `src/Oxygen/Vortex/Types/`:
    `EnvironmentViewData.h`, `ViewColorData.h`, `ViewConstants.h`,
    `ViewConstants.cpp`, and `ViewFrameBindings.h`
  - `ViewConstants.cpp` is now part of the `oxygen-vortex` private source list
  - `oxygen-vortex` builds successfully in Debug after the remaining type
    migration
  - the generated Debug Ninja target query for `oxygen-vortex` shows no
    `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.1` is now **complete**
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - begin step `1.2` with `01-03` to migrate the upload foundation and staging
    slice

### 2026-04-13 — Phase 1 plan 01-01 started with the step-1.1 frame-binding slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Types/CompositingTask.h`
  - `src/Oxygen/Vortex/Types/DebugFrameBindings.h`
  - `src/Oxygen/Vortex/Types/DrawFrameBindings.h`
  - `src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h`
  - `src/Oxygen/Vortex/Types/LightCullingConfig.h`
  - `src/Oxygen/Vortex/Types/LightingFrameBindings.h`
  - `src/Oxygen/Vortex/Types/ShadowFrameBindings.h`
  - `src/Oxygen/Vortex/Types/SyntheticSunData.h`
  - `src/Oxygen/Vortex/Types/VsmFrameBindings.h`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/CompositingTask.h src/Oxygen/Vortex/Types/DebugFrameBindings.h src/Oxygen/Vortex/Types/DrawFrameBindings.h src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h src/Oxygen/Vortex/Types/LightingFrameBindings.h src/Oxygen/Vortex/Types/ShadowFrameBindings.h src/Oxygen/Vortex/Types/VsmFrameBindings.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `rg -n "oxygen-renderer|Oxygen\\.Renderer" out/build-ninja/src/Oxygen/Vortex/CMakeFiles/Export out/build-ninja/src/Oxygen/Vortex/CMakeFiles/oxygen-vortex.dir/Debug/CXXDependInfo.json`
- Result:
  - the first frame-binding half of step `1.1` now lives under
    `src/Oxygen/Vortex/Types/`
  - `LightingFrameBindings.h` now uses Vortex-local
    `LightCullingConfig.h` and `SyntheticSunData.h` so the migrated slice
    carries no `Oxygen/Renderer/` include seam
  - `oxygen-vortex` builds successfully in Debug after the type migration
  - the generated Vortex export/depend info shows no `oxygen-renderer` /
    `Oxygen.Renderer` reference
- Code / validation delta:
  - step `1.1` is **still in progress**; the remaining type headers are
    deferred to `01-02`
  - no broader link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute `01-02` to land the remaining step-1.1 type headers before the
    rest of Phase 1 continues

### 2026-04-13 — Phase 1 plan set repaired

- Changed files this session:
  - `.planning/workstreams/vortex/phases/01-substrate-migration/*-PLAN.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - structural inspection of all Phase 1 plan files
  - per-plan checks for `wave`, `depends_on`, `requirements`, task count,
    `<read_first>`, and `<acceptance_criteria>`
- Result:
  - the full 11-plan Phase 1 set now matches roadmap steps `1.1` through `1.9`
  - `01-04` through `01-06` now cover resources, ScenePrep, and selected
    internals instead of continuing upload work
  - `01-08` now lands only the public half of step `1.7`
  - `01-09` closes the private half of step `1.7`
  - `01-10` now owns step `1.6`, the remaining root-support files, and the
    stripped orchestrator for step `1.8`
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during plan repair
  - Phase 1 is planned and ready to execute, not complete
- Remaining blocker:
  - execute the repaired Phase 1 plan set and collect build/test evidence

### 2026-04-13 — Phase 1 execute-phase blocker recorded

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init execute-phase 1 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 1 --ws vortex`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Result:
  - execute-phase preflight found scope drift between the intended Phase 1 scope and the generated `.planning` micro-plan set
  - `ROADMAP.md` and `01-VALIDATION.md` still require resources, ScenePrep, and internal-utility migration work, but the current `01-04` through `01-06` plan files instead continue the upload migration
  - `01-08-PLAN.md` attempts to record steps `1.4` through `1.7` as complete even though the missing resources / ScenePrep / internal-utility slices are not planned anywhere in the current phase directory
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run because execution was blocked before a trustworthy task boundary existed
  - Phase 1 remains incomplete and must not be reported as having started beyond preflight discovery
- Ledger impact:
  - Phase 1 status is now `blocked`
  - execution must not resume from the current plan set until the missing step coverage is repaired
- Remaining blocker:
  - regenerate or repair the Phase 1 `.planning/workstreams/vortex/phases/01-substrate-migration/*-PLAN.md` files so they explicitly cover steps `1.3`, `1.4`, and `1.5` before implementation resumes

### 2026-04-13 — PLAN.md synchronization

- Changed files this session:
  - `design/vortex/PLAN.md`
- Commands used for verification:
  - repo inspection via `rg`
  - targeted section rereads
  - `git diff -- design/vortex/PLAN.md`
- Result:
  - PLAN.md now explicitly covers Phase 2 activation/validation for `Stencil`
    and `CustomDepth`
  - Phase 5 feature-gated runtime validation now includes `no-shadowing` and
    `no-volumetrics`
  - Phase 7 now maps `MegaLights-class lighting extensions`,
    `Heterogeneous volumes`, and `Hair strands` to explicit future activation
    slots
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run
  - no phase status changed
- Ledger impact:
  - Phase 2 work-item list below must include 2.12 from the updated plan
  - Phase 5 runtime-variant scope below must reflect the expanded PRD §6.6 set
- Remaining blocker:
  - Phase 0 is still incomplete; no implementation progress is claimed from this
    documentation-only session

---

## Phase 0 — Scaffold and Build Integration

**Status:** `done`

### What Exists

| Item | Path | Verified |
| ---- | ---- | -------- |
| Directory tree | `src/Oxygen/Vortex/` (subdirs with `.gitkeep`) | Yes — repo inspection |
| Parent CMake wiring | `src/Oxygen/CMakeLists.txt` | Yes — `add_subdirectory("Vortex")` now present once |
| CMakeLists.txt | `src/Oxygen/Vortex/CMakeLists.txt` | Yes — declares `Oxygen.Vortex`, links deps, C++23 |
| Module anchor source | `src/Oxygen/Vortex/ModuleAnchor.cpp` | Yes — minimal translation unit added so the scaffolded library can generate |
| Export header | `src/Oxygen/Vortex/api_export.h` | Yes — exists |
| Test CMake | `src/Oxygen/Vortex/Test/CMakeLists.txt` | Yes — `Oxygen.Vortex.LinkTest` is enabled and links against `oxygen::vortex` |
| Link smoke source | `src/Oxygen/Vortex/Test/Link_test.cpp` | Yes — minimal consumer includes `Oxygen/Vortex/api_export.h` and exits 0 |

### What Is Missing

None for Phase 0 exit. The standard preset build path, the generated target, and
the alias consumer have all been proven. Remaining work begins in Phase 1.

### Validation Log

| Date | Command | Result |
| ---- | ------- | ------ |
| (initial) | `cmake --build --preset windows-debug --target Oxygen.Vortex --parallel 4` | FAIL — "unknown target 'Oxygen.Vortex'" |
| 2026-04-13 | `rg -n 'add_subdirectory\\("Vortex"\\)' src/Oxygen/CMakeLists.txt` | PASS — parent CMake now wires `Vortex` once |
| 2026-04-13 | `cmake --preset windows-default` | PASS — configure/generate now succeeds with `oxygen-vortex` in the generated project graph |
| 2026-04-13 | `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t targets all \| Select-String 'vortex'` | PASS — generated Debug Ninja graph contains `oxygen-vortex`, `Oxygen.Vortex-d.dll`, and Vortex source-tree targets |
| 2026-04-13 | `cmake --build --preset windows-debug --target help` | FAIL — regeneration blocked in `_deps/ccache.cmake-subbuild` by `ninja: error: failed recompaction: Permission denied` |
| 2026-04-13 | `rg -n 'Oxygen\\.Vortex\\.LinkTest\|oxygen::vortex\|Link_test\\.cpp' src/Oxygen/Vortex/Test/CMakeLists.txt src/Oxygen/Vortex/Test/Link_test.cpp` | PASS — minimal alias smoke target and source are present |
| 2026-04-13 | `cmake --preset windows-default` | PASS — configure/generate succeeds after enabling the link smoke target |
| 2026-04-13 | `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja oxygen-vortex Oxygen.Vortex.LinkTest` | PASS — both the Vortex library and the alias consumer build in Debug |
| 2026-04-13 | `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.LinkTest$' --output-on-failure` | PASS — `Oxygen.Vortex.LinkTest` passes |
| 2026-04-13 | `Remove-Item out/build-ninja/_deps/ccache.cmake-subbuild/.ninja_log{,.restat}; cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest` | PASS — standard preset build path works after resetting the generated ccache subbuild logs |

### Resume Point

Phase 0 is complete. Resume with Phase 1 substrate migration once the Phase 1
design and execution work starts.

---

## Phase 1 — Substrate Migration

**Status:** `done`

### What Exists

- A repaired 11-plan Phase 1 execution set under
  `.planning/workstreams/vortex/phases/01-substrate-migration/`.
- Plan coverage now matches the source-of-truth Vortex design package:
  - `01-04` lands only the prerequisite ABI bundle required by resources
  - `01-05` covers `Resources/*` and closes step `1.3`
  - `01-06` covers only the remaining ScenePrep-only data/config files
  - `01-07` covers ScenePrep execution plus the selected substrate-only
    internal utilities
  - `01-08` covers only the public half of step `1.7`
  - `01-09` closes the private half of step `1.7`
  - `01-10` covers the step-`1.6` pass bases, the remaining root-support
    files, the stripped orchestrator, and the final post-orchestrator
    dependency-edge proof
- Every Phase 1 plan now has 2 tasks plus the required `<read_first>` and
  `<acceptance_criteria>` blocks.
- Step `1.1` is now fully migrated under `src/Oxygen/Vortex/Types/`,
  including the `01-01` frame-binding slice plus
  `EnvironmentViewData.h`, `ViewColorData.h`, `ViewConstants.h`,
  `ViewConstants.cpp`, and `ViewFrameBindings.h`.
- Step `1.2` is now fully migrated under `src/Oxygen/Vortex/Upload/`,
  including upload staging, atlas buffering, inline transfer retirement,
  upload planning, coordinator orchestration, policy, helpers, and tracker
  support.
- Repaired `01-04` is now complete: the prerequisite ABI bundle needed by
  `Resources/*` lives under Vortex ownership and `oxygen-vortex` builds with
  that bundle alone while the resource implementation remains deferred.
- Repaired `01-05` is now complete: the full `Resources/*` slice builds under
  `src/Oxygen/Vortex/Resources/`, and the linked Vortex DLL still carries no
  `Oxygen.Renderer` dependency edge.
- Repaired `01-06` is now complete: the remaining ScenePrep-only data/config
  contracts build under `src/Oxygen/Vortex/ScenePrep/`, while
  `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and
  `ScenePrepPipeline.cpp/.h` remain explicitly deferred to `01-07`.
- Repaired `01-07` is now complete: the deferred ScenePrep execution files and
  selected substrate-only internal utilities build under Vortex ownership, and
  the linked Vortex DLL still carries no `oxygen-renderer` / `Oxygen.Renderer`
  dependency edge.
- Repaired `01-08` is now complete for its owned public slice: the step `1.7`
  root vocabulary (`CompositionView.h`, `RendererCapability.h`,
  `RenderMode.h`) and `SceneRenderer/DepthPrePassPolicy.h` now live under
  Vortex ownership.
- Repaired `01-09` is now complete: the private composition infrastructure
  lives under `src/Oxygen/Vortex/Internal/` and
  `src/Oxygen/Vortex/SceneRenderer/Internal/`, and `oxygen-vortex` builds with
  the full step-`1.7` rehome in place.
- Repaired `01-10` is now complete for its owned code slice: the Vortex root
  support files, pass bases, and stripped renderer shell all build under
  Vortex ownership, and the final post-orchestrator Debug Ninja target query
  still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge.
- Repaired `01-11` is now complete: `Oxygen.Vortex.LinkTest` constructs
  `oxygen::vortex::Renderer` with an empty capability set, drives the stripped
  frame-hook sequence successfully, and the targeted legacy substrate
  regression suite passes in the Debug build tree.
- Repaired `01-12` is now complete for its owned seam-fix slice:
  `FramePlanBuilder` no longer imports `Oxygen/Renderer/*`, and the remaining
  shader-debug/pass-config planning contracts now live under
  `src/Oxygen/Vortex/SceneRenderer/`.
- Repaired `01-13` is now complete: `Oxygen.Vortex.LinkTest` carries a
  Vortex-local hermeticity guard, the full FOUND-03 proof suite passes again,
  and `01-VERIFICATION.md` is now `passed`.

### Steps (from PLAN.md §3)

| Step | Task | Status | Evidence |
| ---- | ---- | ------ | -------- |
| 1.1 | Cross-cutting types (14 headers) | `done` | `01-01` migrated the frame-binding slice; `01-02` landed the remaining type headers, built `oxygen-vortex`, and verified no `Oxygen.Renderer` dependency edge |
| 1.2 | Upload subsystem (14 files) | `done` | `01-03` migrated the full `Upload/` slice, built `oxygen-vortex`, and proved the linked Vortex DLL has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.3 | Resources subsystem (7 files) | `done` | `01-05` landed the full `Resources/*` slice, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `Oxygen.Renderer` dependency edge |
| 1.4 | ScenePrep subsystem (15 files) | `done` | `01-07` landed `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h`, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.5 | Internal utilities (7 files) | `done` | `01-07` landed the selected substrate-only `Internal/*` slice, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.6 | Pass base classes (3 files) | `done` | `01-10` landed `Passes/RenderPass`, `GraphicsRenderPass`, and `ComputeRenderPass` together with the Vortex-owned root contracts, then rebuilt `oxygen-vortex` successfully |
| 1.7 | View assembly + composition | `done` | `01-14` moved the temporary planning/config contracts behind `SceneRenderer/Internal/`, preserved the intended private scene-planning boundary, and verified the repaired Vortex/legacy proof pack |
| 1.8 | Renderer orchestrator | `done` | `01-14` restored renderer-core composition execution, added `kDeferredShading`, reduced the default capability set to truthful substrate families, removed the `oxygen::imgui` link edge, and removed the public `Internal/` header leak |
| 1.9 | Smoke test | `done` | `01-14` expanded the Vortex-side proof surface with `RendererCapability` and `RendererCompositionQueue` regressions plus the hardened `Link_test`, then passed the strengthened proof suite |

### Resume Point

Phase 1 is complete again. Resume with Phase 2 planning/execution from the
repaired substrate baseline.

---

## Phase 2 — SceneTextures + SceneRenderer Shell

**Status:** `done`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.1 SceneTextures four-part contract | `done` |
| D.2 SceneRenderBuilder bootstrap | `done` |
| D.3 SceneRenderer shell dispatch | `done` |

### Work Items (from PLAN.md §4)

| ID | Task | Status | Evidence |
| -- | ---- | ------ | -------- |
| 2.1–2.4 | SceneTextures four-part contract | `done` | `02-06` restored the four-part split, semantic GBuffer naming, and stage-owned setup progression |
| 2.5 | ShadingMode enum | `done` | `02-03` bootstrap/shell proof remains green |
| 2.6 | SceneRenderBuilder | `done` | `02-03` built the shell via `SceneRenderBuilder`, and Phase 2 later closed with explicit human approval |
| 2.7 | SceneRenderer shell (23-stage skeleton) | `done` | `02-03` and `02-08` now prove stage ordering plus multi-view-safe extent policy |
| 2.8 | Wire SceneRenderer into Renderer | `done` | `Renderer.cpp` delegates through the shell while preserving renderer-owned publication/composition seams |
| 2.9 | PostRenderCleanup | `done` | `02-07` now publishes explicit history artifacts instead of live attachment aliases |
| 2.10 | ResolveSceneColor | `done` | `02-07` now publishes explicit resolved artifacts instead of live attachment aliases |
| 2.11 | Stages directory structure | `done` | `02-08` committed `InitViews`, `DepthPrepass`, `Occlusion`, `BasePass`, `Translucency`, `Distortion`, and `LightShaftBloom` stage directories |
| 2.12 | Validate first active `SceneTextures` subset (`SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth`) | `done` | Full `Oxygen.Vortex.*` suite passed, scoped `oxytidy` is clean, and the human review rerun passed |

### Resume Point

Phase 2 is complete. Resume with Phase 3 deferred-core planning.

---

## Phase 3 — Deferred Core

**Status:** `done`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.4 Depth prepass LLD | `done` |
| D.5 Base pass LLD | `done` |
| D.6 Deferred lighting LLD | `done` |
| D.7 Shader contracts LLD | `done` |
| D.8 InitViews LLD | `done` |

### What Exists

- `03-01` is complete: the shared Vortex shader-contract layer exists under
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/` and
  `.../Shared/`.
- `03-02` is complete: the first depth/base Vortex shader entrypoints are
  registered in `EngineShaderCatalog.h` and compile through `ShaderBake`.
- `03-03` is complete: Stage 2 now dispatches through
  `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.*`.
- `03-04` is complete: deferred-core tests now prove InitViews publication and
  active-view prepared-frame rebinding.
- `03-05` is complete: Stage 3 now dispatches through
  `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.*`
  and publishes `depth_prepass_completeness` onto the active view.
- `03-06` is complete: Stage 3 draw processing and publication proof are
  locked to `PreparedSceneFrame` metadata consumption and Stage 3 timing tests.
- `03-07` and `03-08` are complete: Stage 9 now dispatches through a real
  `BasePassModule`, and `BasePassMeshProcessor` consumes published draw
  metadata through `GetDrawMetadata()` / `AcceptedDrawView`.
- `03-09` and `03-10` are complete: Stage 10 remains the first truthful
  GBuffer promotion boundary, and velocity publication stays alive for dynamic
  geometry.
- `03-11` is complete: Phase 3 now carries a runtime-facing deferred
  debug-visualization route through `SceneRenderer`, registered debug shader
  variants for base-color/world-normal/roughness/metalness/scene-depth views,
  and dedicated D3D12 `VortexBasic` capture proof.
- `03-12` is complete: deferred-light shaders compile through the catalog and
  the local-light shader path now avoids the redundant second depth/world
  reconstruction noted in review.
- `03-13` originally landed Stage 12 telemetry/proof seams only; the later
  remediation replaced that shell with actual deferred-light recording.
- `03-14` is complete: the automated proof sweep locks the validation
  contract to the current deferred-core C++ tree and publication vocabulary.
- `03-15` landed a repo-owned closeout runner under `tools/vortex/`.
- `03-16` and `03-17` landed the deferred-core remediation that put
  real Stage 3, Stage 9, and Stage 12 render paths, added reverse-Z-safe
  depth/stencil handling for the new passes, upgraded the deferred-light family
  around the retained bounded-volume local-light path plus procedural local-light
  volume generation, and reran the repo-owned closeout proof successfully.
- `03-18` through `03-21` are complete on the retained runtime branch:
  graphics-layer state continuity is restored, registration/view ownership is
  explicit again, and live `VortexBasic` runtime proof is green on the current
  cleanup-lane build.
- `03-21` is closed. The cleanup lane landed and the final Phase 03 review
  reran the live proof surface against the cleaned branch:
  - hard registry ownership with atomic shared resource/view acquisition
  - explicit Stage 21 resolved scene-color/depth artifacts consumed by
    composition
  - a unified durable VortexBasic RenderDoc wrapper that preserves the probe
    scripts
  - removal of diagnosis-only runtime log spam

### What Is Missing

- No remaining Phase 03 blockers within the current engine feature envelope.
- Deferred skinned / morph engine-enablement stays explicit TODO carry-forward
  work for later phases; it is no longer tracked as an open Phase 03 blocker.

### Resume Point

Phase 3 is permanently closed for the current engine scope. Resume with Phase 4
work; only reopen later closure-gap items if a future audit finds new drift.

---

## Phase 4 — Migration-Critical Services + First Migration

**Status:** `gaps_found`

### Per-Service Status

| Service | Deliverable | Design Status | Impl Status |
| ------- | ----------- | ------------- | ----------- |
| 4A LightingService | D.9 | `done` | `done` |
| 4B PostProcessService | D.10 | `done` | `gaps_found` |
| 4C ShadowService | D.11 | `done` | `done` |
| 4D EnvironmentLightingService | D.12 | `done` | `gaps_found` |
| 4E Examples/Async migration | D.13 | `done` | `gaps_found` |
| 4F Composition/presentation validation | — | — | `gaps_found` |

### Resume Point

Phase 4 is **not** complete on the current branch.

1. The Phase 4 implementation lanes executed and the runtime is now
   substantially wired, but the phase verifier is still `gaps_found`.
2. Direct user UAT on 2026-04-18 rejected the current visible result:
   no visible ImGui overlay composition, poor sky, over-exposed output,
   tonemapping not visually confirmed, and non-credible PBR material output on
   the Async spheres.
3. `04-VERIFICATION.md` and `04-REVIEW.md` are now the authoritative
   closeout state for Phase 4, and they supersede the earlier completion
   wording in this file.
4. Resume with a new Phase 4 gap-planning / remediation pass, not Phase 5.

---

## Phase 5 — Remaining Services + Runtime Scenarios

**Status:** `not_started`

### Per-Item Status

| Item | Deliverable | Design Status | Impl Status |
| ---- | ----------- | ------------- | ----------- |
| 5A DiagnosticsService | D.14 | `not_started` | `not_started` |
| 5B TranslucencyModule | D.15 | `not_started` | `not_started` |
| 5C OcclusionModule | D.16 | `not_started` | `not_started` |
| 5D Multi-view / per-view mode | D.17 | `not_started` | `not_started` |
| 5E Offscreen / facade validation | D.18 | `not_started` | `not_started` |
| 5F Feature-gated runtime variants (`depth-only`, `shadow-only`, `no-environment`, `no-shadowing`, `no-volumetrics`, `diagnostics-only`) | — | — | `not_started` |

### Resume Point

Phase 4 must be completed first. 5A–5E are parallelizable. 5F requires all
services.

---

## Phase 6 — Legacy Deprecation

**Status:** `not_started`

### Resume Point

Phase 5 must be completed first.

---

## Architectural Resume Notes

When implementation resumes, keep these baseline facts explicit:

- The active Vortex source-of-truth package is:
  `PRD.md`, `ARCHITECTURE.md`, `DESIGN.md`, `PROJECT-LAYOUT.md`, `PLAN.md`,
  and this file.
- `DESIGN.md` is the stable design entry point and cross-subsystem rationale
  document. Detailed authoritative per-stage and per-service contracts now live
  under `design/vortex/lld/`; implementation must follow those LLDs rather than
  treating `DESIGN.md` as an incomplete substitute for them.
- Each phase in PLAN.md identifies specific design deliverables that must be
  completed before implementation begins.
- Legacy `Oxygen.Renderer` code remains in the tree only as seam inventory and
  must not be treated as production, fallback, or the source of completion
  criteria for Vortex.
- Referenced historical documents `vortex-initial-design.md` and
  `parity-analysis.md` do not exist in the repo; the current Vortex design
  package supersedes them.
- The only production target recorded by this design package is Vortex.
- Use frame 10 as the RenderDoc baseline capture point.
